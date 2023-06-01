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

const getBufferArraySize = function (bufferArr) {
  let size = 0;
  for (const buffer of bufferArr) {
    size += buffer.length;
  }
  return size;
};

const sleep = (ms) => {
  return new Promise((resolve) => setTimeout(resolve, ms));
};

export default class DataSource {
  constructor(url) {
    this.url = url;

    this.inputOffset = 0;
    this.buffers = [];
    this.totalSize = null;

    this.isInDataRequest = false;
    this.fetchPromise = null;

    // log.info('opening data source', url);
  }

  async init() {
    let details;
    try {
      details = await ResourceLib.getDetails(this.url);
    } catch (err) {
      log.error('error loading file details for', this.url, err);
      this.totalSize = -1;
      return;
    }
    this.totalSize = details.size;
  }

  getUrl() {
    return this.url;
  }

  summarizeActivity() {
    return {
      inputOffset: this.inputOffset,
      buffersSize: this.getLoadedSize(),
      isInDataRequest: this.isInDataRequest,
      isFetching: Boolean(this.fetchPromise),
    };
  }

  getTotalSize() {
    return this.totalSize;
  }

  getLoadedSize() {
    return getBufferArraySize(this.buffers);
  }

  async dataRequest(offset, count) {
    // log.info(`dataRequest ${this.url} offset ${offset} count ${count}, current cache input_offset ${this.input_offset} size ${this.getLoadedSize()}`);
    this.isInDataRequest = true;

    // wait for any pending fetches to finish
    if (this.fetchPromise) {
      // log.info('awaiting fetch_promise');
      await this.fetchPromise;
    }
    const currentSize = this.getLoadedSize();

    const bufferArr = [];
    if (this.inputOffset !== this.totalSize) {
      // do we have all the data being requested already? if so, send it back
      if (offset < this.inputOffset || currentSize === 0 || offset > this.inputOffset + currentSize) {
        // log.info('read of', this.url, 'is before our start of cached data or after the end of cached, there will be a wait as we read from the source', offset, this.input_offset, current_size);
        this.buffers = [];
        await this._fetchInputBlock(offset, count);
        this._addInputBlock(bufferArr, offset);
      } else if (offset + count > this.inputOffset + currentSize) {
        // log.info('we have some data but need more');
        // log.info('starting to send back old input block', offset);
        this._addInputBlock(bufferArr, offset);
        const restCount = offset + count - (this.inputOffset + currentSize);
        const restOffset = this.inputOffset + currentSize;
        if (restOffset !== this.totalSize) {
          // log.info(`rest_count: ${rest_count}, rest_offset: ${rest_offset}`);
          await this._fetchInputBlock(restOffset, restCount);
          // log.info('adding new input block', offset);
          this._addInputBlock(bufferArr, restOffset);
        }
      } else {
        // log.info('have all the blocks we need for', offset, count);
        // pull off the amount we want from this.buffers and resolve it
        this._addInputBlock(bufferArr, offset);
      }

      const bytesToSend = getBufferArraySize(bufferArr);
      // log.info('from', this.url, 'sending in', buffer_arr.length, 'buffers with', bytes_to_send, 'bytes');
      await this._manageCache(offset, bytesToSend);
    }

    // log.info('resolving request with', this.url, getBufferArraySize(buffer_arr));

    this.isInDataRequest = false;
    return bufferArr;
  }

  async _fetchHelper(offset, count) {
    if (offset > this.totalSize) {
      log.error('cannot fetch data starting past end of file', this.url, offset, this.totalSize);
      return null;
    }

    // try to read MIN_REQUEST_SIZE most of the time
    let bytesToRead = Math.max(count, MIN_REQUEST_SIZE);

    if (offset + bytesToRead > this.totalSize) {
      // do not try to read past end of file; the m3u8 in particular could have been rewritten longer than our previously calculated
      // this.total size, and that would confuse things
      bytesToRead = this.totalSize - offset;
    }

    // log.info(`_fetchHelper of ${this.url} at ${offset} count ${count} total_size is ${this.total_size}`);

    if (bytesToRead === 0) {
      log.info('_fetchHelper has no bytes to read, bailing without trying');
      return null;
    }

    let buffer;
    try {
      const promise = ResourceLib.getRangedBufferByUri(this.url, offset, bytesToRead);
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
      log.error('error fetching data', this.url, offset, bytesToRead, err.code, err);
      throw err;
    }
    // log.info('_fetchHelper done fetching', offset, bytes_to_read, buffer.length);

    if (buffer.length < bytesToRead) {
      log.info(`read less than we asked for for ${this.url} ${buffer.length}`);
    }

    this.buffers.push(buffer);
    if (this.buffers.length === 1) {
      this.inputOffset = offset;
    }

    // log.info('_fetchHelper end: data source buffers len', this.url, this.buffers.length);
    // log.info('_fetchHelper end: bytes queued now offset', this.url, this.input_offset, 'size', this.getLoadedSize());

    return null;
  }

  async _fetchInputBlock(offset, count) {
    if (this.fetchPromise) {
      log.error('not allowed to fetch input block while already fetching an input block');
      throw new Error('FetchInputBlockNotReentrant');
    }
    this.fetchPromise = this._fetchHelper(offset, count);
    this.fetchPromise
      .then(() => {
        this.fetchPromise = null;
      })
      .catch((err) => {
        log.error('unexpected error in _fetchHelper', err);
        this.fetchPromise = null;
      });
    return this.fetchPromise;
  }

  // buffer_arr is an array of buffers; we add to it from data we have previously fetched
  _addInputBlock(bufferArr, offset) {
    // log.info('_addInputBlock', this.url, offset, 'have in memory', this.input_offset, this.getLoadedSize());
    let currentOffset = this.inputOffset;

    for (const buffer of this.buffers) {
      if (currentOffset + buffer.length >= offset) {
        const startIndex = Math.max(0, offset - currentOffset);
        const copyCount = buffer.length - startIndex;
        // log.info('_addInputBlock copying', this.url, start_index, copy_count);
        if (copyCount > 0) {
          bufferArr.push(buffer.slice(startIndex, startIndex + copyCount));
        }
      }
      currentOffset += buffer.length;
    }
  }

  // given the last read at offset, count, remove stuff from buffers that we don't think will be useful
  // and read ahead what we think might be useful
  async _manageCache(offset, count) {
    let currentOffset = this.inputOffset;

    // log.info('manageCache', this.url, offset, count);

    // throw away all data before the end of the read that just happened minus BACK_BUFFER_SIZE
    // we have BACK_BUFFER_SIZE in case the next request coming in is just a little bit back from
    // where we were, which we believe may happen often enough (with variable frame rate analysis)
    const newBuffers = [];
    for (const buffer of this.buffers) {
      if (currentOffset >= offset + count - BACK_BUFFER_SIZE) {
        newBuffers.push(buffer);
      } else if (currentOffset + buffer.length < offset + count - BACK_BUFFER_SIZE) {
        // log.info('throwing away a', this.url, 'buffer', buffer.length, 'now at', this.input_offset);
        // do nothing, throw it away
        this.inputOffset += buffer.length;
      } else {
        const partialBuffer = buffer.slice(offset + count - BACK_BUFFER_SIZE - currentOffset);
        newBuffers.push(partialBuffer);
        this.inputOffset += buffer.length - partialBuffer.length;
        // log.info('partial', this.url, partial_buffer.length, 'now at', this.input_offset);
      }

      currentOffset += buffer.length;
    }
    this.buffers = newBuffers;

    // count how much we have past offset + count
    const currentEnd = this.inputOffset + this.getLoadedSize();

    if (currentEnd > this.totalSize) {
      log.error(
        '_manageCache internal calculations went past end of file, should not be possible',
        this.url,
        currentEnd,
        this.totalSize,
      );
      throw new Error('InvalidCalculations');
    }

    if (currentEnd === this.totalSize) {
      // no more to read
      return;
    }

    // log.info('status to consider pre-fetch', current_end, (offset + count), (current_end - (offset + count)), MIN_CACHE_SIZE, this.total_size);
    if (currentEnd - (offset + count) < MIN_CACHE_SIZE) {
      // log.info('pre-fetching', this.url, current_end, MAX_CACHE_SIZE - (current_end - (offset + count)));
      this._fetchInputBlock(currentEnd, MAX_CACHE_SIZE - (currentEnd - (offset + count)));
    }
  }
}
