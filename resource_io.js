/**
 * (c) Chad Walker, Chris Kirmse
 */

import log from './log.js';

import DataSource from './data_source.js';

class ResourceIo {
  constructor(uri) {
    // some versions of libav have trouble handling HLS videos with s3 urls
    // EVEN THOUGH we are doing custom io, so it should NOT care about the url at all.
    // To work around the issue, we just tell it about the path, and handle the true prefix ourselves
    const canonical_url = new URL(uri, 'file:///');
    this.uri_prefix = `${canonical_url.protocol}//${canonical_url.host}`;
    this.primary_uri = canonical_url.pathname;

    // map from url to DataSource
    this.data_sources = {};

    this.latest_open = '';
    this.latest_close = '';
    this.latest_read_uri = '';
    this.total_bytes_read = 0;

    // log.info('resource io constructed', uri);
  }

  getPrimaryUri() {
    return this.primary_uri;
  }

  summarizeActivity() {
    const data_sources = {};
    for (const [url, data_source] of Object.entries(this.data_sources)) {
      data_sources[url] = data_source.summarizeActivity();
    }

    const retval = {
      latest_open: this.latest_open,
      latest_close: this.latest_close,
      latest_read: this.latest_read,
      total_bytes_read: this.total_bytes_read,
      data_sources,
    };

    return retval;
  }

  // below here is called from c++
  async openFile(uri) {
    try {
      // log.info('resource io open file callback called', uri);

      this.latest_open = {input: uri, output: '<running>'};

      let data_source;
      if (Object.hasOwn(this.data_sources, uri)) {
        // already opened
        data_source = this.data_sources[uri];
      } else {
        // re-add the true uri prefix to work around libav issues we do not understand
        const canonical_uri = this.uri_prefix + uri;
        data_source = new DataSource(canonical_uri);

        this.data_sources[uri] = data_source;
        await data_source.init();
      }

      const total_size = data_source.getTotalSize();
      if (total_size < 0) {
        this.latest_open.output = null;
        return null;
      }

      this.latest_open.output = total_size;
      return total_size;
    } catch (err) {
      this.latest_open.output = 'exception';
      log.info('error opening file', uri, err);
      return null;
    }
  }

  closeFile(uri) {
    try {
      // log.info('resource io close file callback called', uri);

      this.latest_close = {input: uri, output: '<running>'};

      if (!Object.hasOwn(this.data_sources, uri)) {
        log.error('close file callback called when file is not open', uri);
        this.latest_close.output = 'error_not_open';
        return;
      }
      delete this.data_sources[uri];
      this.latest_close.output = 'success';
    } catch (err) {
      this.latest_close.output = 'exception';
      log.info('error closing file', uri, err);
    }
  }

  async readFile(uri, offset, count) {
    try {
      // log.info('resource io read file callback called', uri, offset, count);

      this.latest_read = {input: [uri, offset, count], output: '<running>'};
      if (!Object.hasOwn(this.data_sources, uri)) {
        log.error('request data called when file is not open', uri);
        this.latest_read.output = 'error_not_open';
        return null;
      }
      const data_source = this.data_sources[uri];
      const buffer_arr = await data_source.dataRequest(offset, count);
      let bytes_read = 0;
      for (const buffer of buffer_arr) {
        bytes_read += buffer.length;
      }
      this.total_bytes_read += bytes_read;
      this.latest_read.output = bytes_read;
      return buffer_arr;
    } catch (err) {
      this.latest_read.output = 'exception';
      log.info('error reading file', uri, offset, count, err);
      return null;
    }
  }
}

export default ResourceIo;
