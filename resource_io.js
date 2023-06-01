/**
 * (c) Chad Walker, Chris Kirmse
 */

import log from './log.js';

import DataSource from './data_source.js';

export default class ResourceIo {
  constructor(uri) {
    // some versions of libav have trouble handling HLS videos with s3 urls
    // EVEN THOUGH we are doing custom io, so it should NOT care about the url at all.
    // To work around the issue, we just tell it about the path, and handle the true prefix ourselves
    const canonicalUrl = new URL(uri, 'file:///');
    this.uriPrefix = `${canonicalUrl.protocol}//${canonicalUrl.host}`;
    this.primaryUri = canonicalUrl.pathname;

    // map from url to DataSource
    this.dataSources = {};

    this.latestOpen = '';
    this.latestClose = '';
    this.latestReadUri = '';
    this.totalBytesRead = 0;

    // log.info('resource io constructed', uri);
  }

  getPrimaryUri() {
    return this.primaryUri;
  }

  summarizeActivity() {
    const dataSources = {};
    for (const [url, dataSource] of Object.entries(this.dataSources)) {
      dataSources[url] = dataSource.summarizeActivity();
    }

    const retval = {
      latestOpen: this.latestOpen,
      latestClose: this.latestClose,
      latestRead: this.latestRead,
      totalBytes_read: this.totalBytesRead,
      dataSources: dataSources,
    };

    return retval;
  }

  // below here is called from c++
  async openFile(uri) {
    try {
      // log.info('resource io open file callback called', uri);

      this.latestOpen = { input: uri, output: '<running>' };

      let dataSource;
      if (Object.hasOwn(this.dataSources, uri)) {
        // already opened
        dataSource = this.dataSources[uri];
      } else {
        // re-add the true uri prefix to work around libav issues we do not understand
        const canonicalUri = this.uriPrefix + uri;
        dataSource = new DataSource(canonicalUri);

        this.dataSources[uri] = dataSource;
        await dataSource.init();
      }

      const totalSize = dataSource.getTotalSize();
      if (totalSize < 0) {
        this.latestOpen.output = null;
        return null;
      }

      this.latestOpen.output = totalSize;
      return totalSize;
    } catch (err) {
      this.latestOpen.output = 'exception';
      log.info('error opening file', uri, err);
      return null;
    }
  }

  closeFile(uri) {
    try {
      // log.info('resource io close file callback called', uri);

      this.latestClose = { input: uri, output: '<running>' };

      if (!Object.hasOwn(this.dataSources, uri)) {
        log.error('close file callback called when file is not open', uri);
        this.latestClose.output = 'error_not_open';
        return;
      }
      delete this.dataSources[uri];
      this.latestClose.output = 'success';
    } catch (err) {
      this.latestClose.output = 'exception';
      log.info('error closing file', uri, err);
    }
  }

  async readFile(uri, offset, count) {
    try {
      // log.info('resource io read file callback called', uri, offset, count);

      this.latestRead = { input: [uri, offset, count], output: '<running>' };
      if (!Object.hasOwn(this.dataSources, uri)) {
        log.error('request data called when file is not open', uri);
        this.latestRead.output = 'error_not_open';
        return null;
      }
      const dataSource = this.dataSources[uri];
      const bufferArr = await dataSource.dataRequest(offset, count);
      let bytesRead = 0;
      for (const buffer of bufferArr) {
        bytesRead += buffer.length;
      }
      this.totalBytesRead += bytesRead;
      this.latestRead.output = bytesRead;
      return bufferArr;
    } catch (err) {
      this.latestRead.output = 'exception';
      log.info('error reading file', uri, offset, count, err);
      return null;
    }
  }
}
