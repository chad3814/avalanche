/**
 * (c) Chad Walker, Chris Kirmse
 */

import fs from 'fs';
import path from 'path';
import stream from 'stream';

import mime from 'mime';
import request from 'request';

const {
  promises: pfs,
  constants: { R_OK, W_OK },
} = fs;

const ResourceLib = {};

const noop = function () {
  /**/
};
const UNKNOWN_MIME_TYPE = 'application/octet-stream';

const DEFAULT_TIMEOUT_MS = 10 * 60 * 1000;

class CountingStream extends stream.PassThrough {
  constructor(options) {
    super(options);
    this._bytesIn = 0;
    this._bytesOut = 0;
    this._expectedBytes = null;
    this._stoppered = true;
    this._stopperedData = [];
  }

  unstop() {
    if (!this._stoppered) {
      return;
    }
    for (const data of this._stopperedData) {
      this.push(data);
      this._bytesOut += data.length;
      this.emit('progress', this.getProgress());
    }
    this._stoppered = false;
    this._stopperedData.length = 0;
  }

  setExpectedBytes(bytes) {
    this._expectedBytes = bytes;
  }

  getExpectedBytes() {
    return this._expectedBytes;
  }

  getProgress() {
    const progress = {
      stoppered: this._stoppered,
      bytesIn: this._bytesIn,
      bytesOut: this._bytesOut,
      expectedBytes: this._expectedBytes,
    };

    return progress;
  }

  _transform(data, encoding, callback) {
    if (encoding !== 'buffer') {
      data = Buffer.from(data, encoding);
    }
    this._bytesIn += data.length;
    this.emit('progress', this.getProgress());
    if (this._stoppered) {
      this._stopperedData.push(data);
      return setImmediate(callback);
    }
    this.push(data);
    this._bytesOut += data.length;
    this.emit('progress', this.getProgress());
    return callback();
  }

  _flush(callback) {
    this.unstop();
    return callback();
  }
}

// all streams resolved here get an `rlDestroy()` method
// added to them that will destroy the underlying resource
// when called, fixing a slightly broken implementation of
// `stream.PassThrough()`
ResourceLib.getReadStreamByUri = async function (uri, options = {}, readProgressCallback = noop) {
  let totalBytes;

  const url = new URL(uri, 'file:///');
  switch (url.protocol) {
    case 'http:':
    case 'https:': {
      return new Promise((resolve, reject) => {
        const httpOptions = {
          url: uri,
        };
        if (options.range) {
          httpOptions.headers = {
            Range: `bytes=${options.range.start}-${options.range.end}`,
          };
        }

        const req = request(httpOptions);
        const readStream = new CountingStream();
        readStream.rlDestroy = () => req.abort();
        req.on('error', (error) => reject(error));
        readStream.on('error', (err) => reject(err));
        req.on('response', (response) => {
          if (response.statusCode >= 200 && response.statusCode < 300) {
            totalBytes = parseInt(response.headers['content-length'], 10);
            readStream.setExpectedBytes(totalBytes);
            resolve(readStream);
            return readStream.unstop();
          }
          reject(new Error('BadStatusCode'));
          return req.once('complete', ({ headers, statusCode }, body) => {
            log.info('error getting uri', uri, 'got status code', statusCode);
            log.info('--response headers', headers);
            log.info('--response body:', body);
          });
        });
        readStream.on('progress', (progress) => {
          readProgressCallback({
            step: progress.bytes_out,
            total: totalBytes,
          });
        });
        req.pipe(readStream);
        readStream.on('end', () => {
          const progress = readStream.getProgress();
          readProgressCallback({
            step: progress.bytes_out,
            total: totalBytes,
          });
        });
        return null;
      });
    }
    case 'file:': {
      uri = decodeURIComponent(url.pathname); // normalized path
      await pfs.access(uri, R_OK); // this throws if access is denied
      const fsOptions = {};
      if (options.range) {
        fsOptions.start = options.range.start;
        fsOptions.end = options.range.end;
      }
      const stats = await pfs.stat(uri);
      totalBytes = stats.size;
      const readStream = fs.createReadStream(uri, fsOptions);
      const passThrough = new CountingStream();
      passThrough.unstop();
      passThrough.rlDestroy = () => readStream.destroy();
      passThrough.setExpectedBytes(totalBytes);
      passThrough.on('progress', (progress) => {
        readProgressCallback({
          step: progress.bytes_out,
          total: totalBytes,
        });
      });
      readStream.pipe(passThrough);
      return passThrough;
    }
    default:
      log.error('unhandled stream protocol:', url.protocol);
      throw new Error('UnknownProtocol');
  }
};

ResourceLib.getBufferFromStream = async function (sourceStream) {
  return new Promise((resolve, reject) => {
    const buffers = [];
    let cleanup = null;

    const bufferedRead = function (chunk) {
      buffers.push(chunk);
    };

    const bufferError = function (err) {
      cleanup();
      reject(err);
    };

    const bufferEnd = function () {
      cleanup();
      resolve(Buffer.concat(buffers));
    };

    sourceStream.on('data', bufferedRead);
    sourceStream.on('error', bufferError);
    sourceStream.on('end', bufferEnd);
    cleanup = function () {
      sourceStream.off('data', bufferedRead);
      sourceStream.off('error', bufferError);
      sourceStream.off('end', bufferEnd);
    };
  });
};

ResourceLib.getBufferFromUri = async function (uri, options) {
  options = options || {};
  let numTries = 0;
  const maxAttempts = options.max_attempts || 5;
  while (true) {
    let buffer;
    try {
      const readStream = await ResourceLib.getReadStreamByUri(uri, options);
      buffer = await ResourceLib.getBufferFromStream(readStream);
    } catch (err) {
      if (err.message === 'BadStatusCode' || err.code === 'TimeoutError' || err.code === 'NetworkingError') {
        log.info('retryable error while getting buffer from stream', err.code);
        numTries++;
        if (numTries >= maxAttempts) {
          log.info('too many errors getting buffer from stream');
          throw err;
        }
        await utils.sleep(Math.round(Math.exp(numTries)) * 1000);
        log.info('retrying get buffer from uri');
        continue;
      }
      throw err;
    }
    return buffer;
  }
};

ResourceLib.getStringFromUri = async function (uri, options) {
  const buffer = await ResourceLib.getBufferFromUri(uri, options);
  return buffer.toString('utf8');
};

// promise is resolved when the stream is available (virtually immediately)
ResourceLib.getWriteStreamByUri = async function (uri, out = {}) {
  await ResourceLib.makeParentDirectoriesByUri(uri);
  // need to disable eslint warning here because we do some async stuff _after_ resolving the promise... this is very unusual
  // FIXME The lint rule is flagging this due to two potential issues:
  //  1. Any uncaught exceptions in an async executor will **not** cause the `Promise` to reject.
  //  2. Either the `new Promise()` is not required or the scope of the executor is too broad.
  // In this case, it appears that the parent `switch` does not need to be wrapped in a `Promise`.
  // eslint-disable-next-line no-async-promise-executor
  return new Promise(async (resolve, reject) => {
    const url = new URL(uri, 'file:///');
    switch (url.protocol) {
      case 'http:':
      case 'https:':
        return reject(new Error('CantWriteToHttp'));
      case 'file:': {
        uri = url.pathname; // normalized path
        await pfs.access(path.dirname(uri), W_OK); // throws on no access
        const passThrough = new CountingStream();
        passThrough.unstop(); // for write streams we immediately unstop.
        const writeStream = fs.createWriteStream(uri);
        passThrough.pipe(writeStream);
        resolve(passThrough);

        writeStream.on('close', () => {
          passThrough.emit('rl_done');
        });
        return null;
      }
      default:
        log.error('unhandled stream protocol:', url.protocol);
        return reject(new Error('UnknownProtocol'));
    }
  });
};

ResourceLib.writeBufferToUri = async function (buffer, uri) {
  const out = {};
  const writeStream = await ResourceLib.getWriteStreamByUri(uri, out);

  return new Promise((resolve, reject) => {
    writeStream.on('error', (err) => {
      if (uri.startsWith('s3:') && out.req) {
        log.error('error writing buffer to s3:', uri, err);
        if (out.req.singlePart && out.req.singlePart.response && out.req.singlePart.response.httpResponse) {
          log.error('response headers for', uri, out.req.singlePart.response.httpResponse.headers);
          log.error('response body for', uri, out.req.singlePart.response.httpResponse.body.toString('utf8'));
        }
      }
      return reject(err);
    });

    writeStream.on('rl_done', () => {
      const progress = writeStream.getProgress();
      resolve(progress.bytes_out);
    });

    writeStream.end(buffer);
  });
};

ResourceLib.writeStringToUri = async function (str, uri) {
  return ResourceLib.writeBufferToUri(Buffer.from(str), uri);
};

ResourceLib.copyFromStreamToUri = async function (
  sourceStream,
  destUri,
  writeProgressCallback = noop,
  timeoutMs = DEFAULT_TIMEOUT_MS,
) {
  let cleanup;
  let timeoutId;
  const timeoutError = new Error('Timeout');
  timeoutError.code = 'TimeoutError';

  const out = {};
  const destStream = await ResourceLib.getWriteStreamByUri(destUri, out);

  return new Promise((resolve, reject) => {
    let done = function (err) {
      cleanup();
      if (err) {
        return reject(err);
      }
      const progress = destStream.getProgress();
      return resolve(progress.bytes_out);
    };
    const error = function (err) {
      cleanup();
      if (this === sourceStream) {
        log.error('error copying file from source_stream to uri:', destUri, err);
        return reject(err);
      }
      log.info('error copying file to dest_stream:', destUri, err);
      if (destUri.startsWith('s3:') && out.req) {
        log.error('error copying stream to s3:', err);
        if (out.req.singlePart && out.req.singlePart.response && out.req.singlePart.response.httpResponse) {
          log.error('response headers for', destUri, out.req.singlePart.response.httpResponse.headers);
          log.error('response body for', destUri, out.req.singlePart.response.httpResponse.body.toString('utf8'));
        }
      }
      return reject(err);
    };
    cleanup = function () {
      done = noop;
      if (timeoutId) {
        clearTimeout(timeoutId);
      }
      if (sourceStream.off) {
        sourceStream.off('error', error);
      }
      if (destStream.off) {
        destStream.off('error', error);
      }
    };
    sourceStream.on('error', error);
    destStream.on('error', error);
    destStream.on('rl_done', done);

    destStream.on('progress', (progress) => {
      if (timeoutId) {
        clearTimeout(timeoutId);
        timeoutId = setTimeout(error, timeoutMs, timeoutError);
      }
      let expectedBytes = progress.expectedBytes;
      if (!expectedBytes && sourceStream.getExpectedBytes) {
        expectedBytes = sourceStream.getExpectedBytes();
        destStream.setExpectedBytes(expectedBytes);
      }

      writeProgressCallback({
        step: progress.bytesOut,
        total: expectedBytes,
      });
    });
    destStream.on('end', () => {
      if (timeoutId) {
        clearTimeout(timeoutId);
        timeoutId = null;
      }
      const progress = destStream.getProgress();
      writeProgressCallback({
        step: progress.bytesOut,
        total: progress.expectedBytes,
      });
    });

    if (timeoutMs !== 0) {
      timeoutId = setTimeout(error, timeoutMs, timeoutError);
    }
    sourceStream.pipe(destStream);
  });
};

ResourceLib.copyByUris = async function (
  sourceUri,
  destUri,
  readProgressCallback = noop,
  writeProgressCallback = noop,
  timeoutMs = DEFAULT_TIMEOUT_MS,
) {
  // see if we have an optimized implementation
  const sourceUrl = new URL(sourceUri, 'file:///');
  const destUrl = new URL(destUri, 'file:///');

  const sourceStream = await ResourceLib.getReadStreamByUri(sourceUri, {}, readProgressCallback);
  return ResourceLib.copyFromStreamToUri(sourceStream, destUri, writeProgressCallback, timeoutMs);
};

ResourceLib.copyByUrisWithRetries = async function (
  sourceUri,
  destUri,
  readProgressCallback = noop,
  writeProgressCallback = noop,
  timeoutMs = DEFAULT_TIMEOUT_MS,
  maxAttempts = 5,
) {
  return utils.callWithRetries(
    () => ResourceLib.copyByUris(sourceUri, destUri, readProgressCallback, writeProgressCallback, timeoutMs),
    maxAttempts,
  );
};

ResourceLib.getDirFilenames = async function (uri, regex = /.+/u, options = {}) {
  const url = new URL(uri, 'file:///');
  switch (url.protocol) {
    case 'http:':
    case 'https:':
      throw new Error('CantGetFilesFromHttp');
    case 'file:': {
      uri = decodeURIComponent(url.pathname); // normalized path
      const filenames = [];
      const files = await pfs.readdir(uri);
      for (const file of files) {
        if (regex.test(file)) {
          filenames.push(file);
        }
      }
      return filenames;
    }
    default:
      log.error('unhandled stream protocol:', url.protocol);
      throw new Error('UnknownProtocol');
  }
};

ResourceLib.getDirSubdirNames = async function (uri, regex = /.+/u, options = {}) {
  const url = new URL(uri, 'file:///');
  switch (url.protocol) {
    case 'http:':
    case 'https:':
      throw new Error('CantGetFilesFromHttp');
    case 'file:': {
      uri = decodeURIComponent(url.pathname); // normalized path
      const filenames = [];
      const files = await pfs.readdir(uri);
      for (const file of files) {
        if (!regex.test(file)) {
          continue;
        }
        const stat = await pfs.stat(path.join(uri, file));
        if (!stat.isDirectory()) {
          continue;
        }
        filenames.push(file);
      }
      return filenames;
    }
    default:
      log.error('unhandled stream protocol:', url.protocol);
      throw new Error('UnknownProtocol');
  }
};

// When called on a file path, this returns the path from the uri
// i.e. if the uri is file:///tmp and are files in it are
// foo/bar.js and baz.js, the output will be ['foo/bar.js', and 'baz.js']
ResourceLib.getDirFilenamesTree = async function (uri, regex = /.+/u, options = {}) {
  const url = new URL(uri, 'file:///');
  switch (url.protocol) {
    case 'http:':
    case 'https:':
      throw new Error('CantGetFilesFromHttp');
    case 'file:': {
      uri = decodeURIComponent(url.pathname); // normalized path
      const retval = [];
      const dirs = [''];
      while (dirs.length > 0) {
        const dir = dirs.shift();
        const list = await pfs.readdir(`${uri}/${dir}`);
        for (const item of list) {
          const filename = path.format({
            dir,
            base: item,
          });
          if (regex.test(filename)) {
            retval.push(filename);
            continue;
          }

          const stats = await pfs.stat(`${uri}/${filename}`);
          if (stats.isDirectory()) {
            dirs.push(`${filename}`);
          }
        }
      }
      return retval;
    }
    default:
      log.error('unhandled stream protocol:', url.protocol);
      throw new Error('UnknownProtocol');
  }
};

// When called on a file path, this returns the full filepath
// i.e. if the uri is file:///tmp and are files in it are
// foo/bar.js and baz.js, the output will be ['/tmp/foo/bar.js', and '/tmp/baz.js']

ResourceLib.getDirFullFilenamesTree = async function (uri, regex = /.+/u, options = {}) {
  const url = new URL(uri, 'file:///');
  switch (url.protocol) {
    case 'http:':
    case 'https:':
      throw new Error('CantGetFilesFromHttp');
    case 'file:': {
      uri = decodeURIComponent(url.pathname); // normalized path
      const retval = [];
      const dirs = [uri];
      while (dirs.length > 0) {
        const dir = dirs.shift();
        const list = await pfs.readdir(dir);
        for (const item of list) {
          const filename = path.format({
            dir,
            base: item,
          });
          if (regex.test(filename)) {
            retval.push(filename);
            continue;
          }

          const stats = await pfs.stat(filename);
          if (stats.isDirectory()) {
            dirs.push(filename);
          }
        }
      }
      return retval;
    }
    default:
      log.error('unhandled stream protocol:', url.protocol);
      throw new Error('UnknownProtocol');
  }
};

ResourceLib.doesUriExist = async function (uri) {
  try {
    await ResourceLib.getDetails(uri);
  } catch (err) {
    if (err.message === 'NotFound') {
      return false;
    }
    throw err;
  }
  return true;
};

ResourceLib.doesUriDirExist = async function (uri) {
  const url = new URL(uri, 'file:///');
  switch (url.protocol) {
    case 'http:':
    case 'https:':
      throw new Error('CantUriDirExistHttp');
    case 'file:': {
      uri = decodeURIComponent(url.pathname); // normalized path
      let stat;
      try {
        stat = await pfs.stat(uri);
      } catch (err) {
        return false;
      }
      if (!stat.isDirectory()) {
        return false;
      }
      return true;
    }
    default:
      log.error('unhandled protocol for doesUriDirExist:', url.protocol);
      throw new Error('UnknownProtocol');
  }
};

ResourceLib.eraseByUri = async function (uri) {
  const url = new URL(uri, 'file:///');
  switch (url.protocol) {
    case 'http:':
    case 'https:':
      throw new Error('CannotEraseFilesFromHttp');
    case 'file:': {
      uri = decodeURIComponent(url.pathname); // normalized path
      await pfs.unlink(uri);
      return;
    }
    default:
      log.error('unhandled stream protocol:', url.protocol);
      throw new Error('UnknownProtocol');
  }
};

ResourceLib.getDetails = async function (uri) {
  const url = new URL(uri, 'file:///');
  switch (url.protocol) {
    case 'http:':
    case 'https:': {
      return new Promise((resolve, reject) =>
        request.head(uri, (err, response) => {
          if (err) {
            log.error('error HEADing', uri, err);
            return reject(err);
          }
          if (response.statusCode === 404) {
            return reject(new Error('NotFound', `${uri} does not exist`));
          }
          if (response.statusCode < 200 || response.statusCode >= 300) {
            return reject(new Error('BadStatusCode'));
          }
          const details = {
            size: parseInt(response.headers['content-length'], 10),
            modified: response.headers['last-modified'],
            type: response.headers['content-type'],
          };
          return resolve(details);
        }),
      );
    }
    case 'file:': {
      uri = decodeURIComponent(url.pathname); // normalized path
      let stat;
      try {
        stat = await pfs.stat(uri);
      } catch (err) {
        if (err.code === 'ENOENT') {
          throw new Error('NotFound', `${uri} does not exist`);
        }
        throw Error.create(err);
      }

      const details = {
        size: stat.size,
        modified: stat.mtime,
      };
      details.type = mime.getType(uri) || UNKNOWN_MIME_TYPE;
      if (stat.isDirectory()) {
        details.size = 0;
        details.type = 'directory';
      }
      return details;
    }
    default:
      log.error('unhandled stream protocol:', url.protocol);
      throw new Error('UnknownProtocol');
  }
};

ResourceLib.makeParentDirectoriesByUri = async function (uri) {
  const url = new URL(uri, 'file:///');
  switch (url.protocol) {
    case 'http:':
    case 'https:':
      throw new Error('CannotMakeDirHttp');
    case 'file:': {
      uri = decodeURIComponent(url.pathname); // normalized path
      await pfs.mkdir(path.dirname(uri), { recursive: true });
      return;
    }
    default:
      log.error('unhandled stream protocol:', url.protocol);
      throw new Error('UnknownProtocol');
  }
};

ResourceLib.getRangedBufferByUri = async function (uri, offset, size) {
  // range option is inclusive on both start and end; hence the `- 1`
  const options = {
    range: {
      start: offset,
      end: offset + size - 1,
    },
  };
  if (size === -1) {
    delete options.range.end;
  }
  return ResourceLib.getBufferFromUri(uri, options);
};

export default ResourceLib;
