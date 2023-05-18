/**
 * (c) Chad Walker, Chris Kirmse
 */

import { timeout, TimeoutError } from 'promise-timeout';

import log from './log.js';
import ResourceLib from './resource_lib.js';

const MIN_REQUEST_SIZE = 500000;

const MIN_CACHE_SIZE = 1000000;
const MAX_CACHE_SIZE = 3000000;

const BACK_BUFFER_SIZE = 1000000;

const MAX_RESOURCE_REQUEST_MS = 5 * 60 * 1000;

const getBufferArraySize = function (buffer_arr) {
  let size = 0;
  for (const buffer of buffer_arr) {
    size += buffer.length;
  }
  return size;
};

const sleep = (ms) => {
  return new Promise(resolve => setTimeout(resolve, ms));
};

class DataSource {
  constructor(url) {
    this.url = url;

    this.input_offset = 0;
    this.buffers = [];
    this.total_size = null;

    this.is_in_data_request = false;
    this.fetch_promise = null;

    // log.info('opening data source', url);
  }

  async init() {
    let details;
    try {
      details = await ResourceLib.getDetails(this.url);
    } catch (err) {
      log.error('error loading file details for', this.url, err);
      this.total_size = -1;
      return;
    }
    this.total_size = details.size;
  }

  getUrl() {
    return this.url;
  }

  summarizeActivity() {
    return {
      input_offset: this.input_offset,
      buffers_size: this.getLoadedSize(),
      is_in_data_request: this.is_in_data_request,
      is_fetching: Boolean(this.fetch_promise),
    };
  }

  getTotalSize() {
    return this.total_size;
  }

  getLoadedSize() {
    return getBufferArraySize(this.buffers);
  }

  async dataRequest(offset, count) {
    // log.info(`dataRequest ${this.url} offset ${offset} count ${count}, current cache input_offset ${this.input_offset} size ${this.getLoadedSize()}`);
    this.is_in_data_request = true;

    // wait for any pending fetches to finish
    if (this.fetch_promise) {
      // log.info('awaiting fetch_promise');
      await this.fetch_promise;
    }
    const current_size = this.getLoadedSize();

    const buffer_arr = [];
    if (this.input_offset !== this.total_size) {
      // do we have all the data being requested already? if so, send it back
      if (
        offset < this.input_offset ||
        current_size === 0 ||
        offset > this.input_offset + current_size
      ) {
        // log.info('read of', this.url, 'is before our start of cached data or after the end of cached, there will be a wait as we read from the source', offset, this.input_offset, current_size);
        this.buffers = [];
        await this._fetchInputBlock(offset, count);
        this._addInputBlock(buffer_arr, offset);
      } else if (offset + count > this.input_offset + current_size) {
        // log.info('we have some data but need more');
        // log.info('starting to send back old input block', offset);
        this._addInputBlock(buffer_arr, offset);
        const rest_count = offset + count - (this.input_offset + current_size);
        const rest_offset = this.input_offset + current_size;
        if (rest_offset !== this.total_size) {
          // log.info(`rest_count: ${rest_count}, rest_offset: ${rest_offset}`);
          await this._fetchInputBlock(rest_offset, rest_count);
          // log.info('adding new input block', offset);
          this._addInputBlock(buffer_arr, rest_offset);
        }
      } else {
        // log.info('have all the blocks we need for', offset, count);
        // pull off the amount we want from this.buffers and resolve it
        this._addInputBlock(buffer_arr, offset);
      }

      const bytes_to_send = getBufferArraySize(buffer_arr);
      // log.info('from', this.url, 'sending in', buffer_arr.length, 'buffers with', bytes_to_send, 'bytes');
      await this._manageCache(offset, bytes_to_send);
    }

    // log.info('resolving request with', this.url, getBufferArraySize(buffer_arr));

    this.is_in_data_request = false;
    return buffer_arr;
  }

  async _fetchHelper(offset, count) {
    if (offset > this.total_size) {
      log.error(
        'cannot fetch data starting past end of file',
        this.url,
        offset,
        this.total_size,
      );
      return null;
    }

    // try to read MIN_REQUEST_SIZE most of the time
    let bytes_to_read = Math.max(count, MIN_REQUEST_SIZE);

    if (offset + bytes_to_read > this.total_size) {
      // do not try to read past end of file; the m3u8 in particular could have been rewritten longer than our previously calculated
      // this.total size, and that would confuse things
      bytes_to_read = this.total_size - offset;
    }

    // log.info(`_fetchHelper of ${this.url} at ${offset} count ${count} total_size is ${this.total_size}`);

    if (bytes_to_read === 0) {
      log.info('_fetchHelper has no bytes to read, bailing without trying');
      return null;
    }

    let buffer;
    try {
      const promise = ResourceLib.getRangedBufferByUri(
        this.url,
        offset,
        bytes_to_read,
      );
      buffer = await timeout(promise, MAX_RESOURCE_REQUEST_MS);
    } catch (err) {
      if (err instanceof TimeoutError) {
        log.info('utils promise timeout fired, will retry:', err);
        await sleep(500);
        return this._fetchHelper(offset, count);
      }
      if (err.code === 'InvalidRange') {
        // not too common but this can happen if the last read we did just got to the end of the file,
        // then we did a read here that started past the end of the file. So, handle it by just saying
        // that input is done
        return null;
      } else if (err.code === 'TimeoutError') {
        log.info('TimeoutError, will retry:', err);
        await sleep(500);
        return this._fetchHelper(offset, count);
      } else if (err.code === 'StreamContentLengthMismatch') {
        log.info('StreamContentLengthMismatch, will retry:', err);
        await sleep(500);
        return this._fetchHelper(offset, count);
      } else if (err.code === 'NetworkingError') {
        log.info('NetworkingError, will retry:', err);
        await sleep(500);
        return this._fetchHelper(offset, count);
      }
      log.error(
        'error fetching data',
        this.url,
        offset,
        bytes_to_read,
        err.code,
        err,
      );
      throw err;
    }
    // log.info('_fetchHelper done fetching', offset, bytes_to_read, buffer.length);

    this.bytes_read += buffer.length;

    if (buffer.length < bytes_to_read) {
      log.info(`read less than we asked for for ${this.url} ${buffer.length}`);
    }

    this.buffers.push(buffer);
    if (this.buffers.length === 1) {
      this.input_offset = offset;
    }

    // log.info('_fetchHelper end: data source buffers len', this.url, this.buffers.length);
    // log.info('_fetchHelper end: bytes queued now offset', this.url, this.input_offset, 'size', this.getLoadedSize());

    return null;
  }

  async _fetchInputBlock(offset, count) {
    if (this.fetch_promise) {
      log.error(
        'not allowed to fetch input block while already fetching an input block',
      );
      throw new Error('FetchInputBlockNotReentrant');
    }
    this.fetch_promise = this._fetchHelper(offset, count);
    this.fetch_promise
      .then(() => {
        this.fetch_promise = null;
      })
      .catch((err) => {
        log.error('unexpected error in _fetchHelper', err);
        this.fetch_promise = null;
      });
    return this.fetch_promise;
  }

  // buffer_arr is an array of buffers; we add to it from data we have previously fetched
  _addInputBlock(buffer_arr, offset) {
    // log.info('_addInputBlock', this.url, offset, 'have in memory', this.input_offset, this.getLoadedSize());
    let current_offset = this.input_offset;

    for (const buffer of this.buffers) {
      if (current_offset + buffer.length >= offset) {
        const start_index = Math.max(0, offset - current_offset);
        const copy_count = buffer.length - start_index;
        // log.info('_addInputBlock copying', this.url, start_index, copy_count);
        if (copy_count > 0) {
          buffer_arr.push(buffer.slice(start_index, start_index + copy_count));
        }
      }
      current_offset += buffer.length;
    }
  }

  // given the last read at offset, count, remove stuff from buffers that we don't think will be useful
  // and read ahead what we think might be useful
  async _manageCache(offset, count) {
    let current_offset = this.input_offset;

    // log.info('manageCache', this.url, offset, count);

    // throw away all data before the end of the read that just happened minus BACK_BUFFER_SIZE
    // we have BACK_BUFFER_SIZE in case the next request coming in is just a little bit back from
    // where we were, which we believe may happen often enough (with variable frame rate analysis)
    const new_buffers = [];
    for (const buffer of this.buffers) {
      if (current_offset >= offset + count - BACK_BUFFER_SIZE) {
        new_buffers.push(buffer);
      } else if (
        current_offset + buffer.length <
        offset + count - BACK_BUFFER_SIZE
      ) {
        // log.info('throwing away a', this.url, 'buffer', buffer.length, 'now at', this.input_offset);
        // do nothing, throw it away
        this.input_offset += buffer.length;
      } else {
        const partial_buffer = buffer.slice(
          offset + count - BACK_BUFFER_SIZE - current_offset,
        );
        new_buffers.push(partial_buffer);
        this.input_offset += buffer.length - partial_buffer.length;
        // log.info('partial', this.url, partial_buffer.length, 'now at', this.input_offset);
      }

      current_offset += buffer.length;
    }
    this.buffers = new_buffers;

    // count how much we have past offset + count
    const current_end = this.input_offset + this.getLoadedSize();

    if (current_end > this.total_size) {
      log.error(
        '_manageCache internal calculations went past end of file, should not be possible',
        this.url,
        current_end,
        this.total_size,
      );
      throw new Error('InvalidCalculations');
    }

    if (current_end === this.total_size) {
      // no more to read
      return;
    }

    // log.info('status to consider pre-fetch', current_end, (offset + count), (current_end - (offset + count)), MIN_CACHE_SIZE, this.total_size);
    if (current_end - (offset + count) < MIN_CACHE_SIZE) {
      // log.info('pre-fetching', this.url, current_end, MAX_CACHE_SIZE - (current_end - (offset + count)));
      this._fetchInputBlock(
        current_end,
        MAX_CACHE_SIZE - (current_end - (offset + count)),
      );
    }
  }
}

export default DataSource;
