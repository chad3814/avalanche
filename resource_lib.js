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
  constants: {R_OK, W_OK},
} = fs;

const ResourceLib = {};

const noop = function () {};
const UNKNOWN_MIME_TYPE = 'application/octet-stream';

const DEFAULT_TIMEOUT_MS = 10 * 60 * 1000;

class CountingStream extends stream.PassThrough {
  constructor(options) {
    super(options);
    this._bytes_in = 0;
    this._bytes_out = 0;
    this._expected_bytes = null;
    this._stoppered = true;
    this._stoppered_data = [];
  }

  unstop() {
    if (!this._stoppered) {
      return;
    }
    for (const data of this._stoppered_data) {
      this.push(data);
      this._bytes_out += data.length;
      this.emit('progress', this.getProgress());
    }
    this._stoppered = false;
    this._stoppered_data.length = 0;
  }

  setExpectedBytes(bytes) {
    this._expected_bytes = bytes;
  }

  getExpectedBytes() {
    return this._expected_bytes;
  }

  getProgress() {
    const progress = {
      stoppered: this._stoppered,
      bytes_in: this._bytes_in,
      bytes_out: this._bytes_out,
      expected_bytes: this._expected_bytes,
    };

    return progress;
  }

  _transform(data, encoding, callback) {
    if (encoding !== 'buffer') {
      data = Buffer.from(data, encoding);
    }
    this._bytes_in += data.length;
    this.emit('progress', this.getProgress());
    if (this._stoppered) {
      this._stoppered_data.push(data);
      return setImmediate(callback);
    }
    this.push(data);
    this._bytes_out += data.length;
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
ResourceLib.getReadStreamByUri = async function (
  uri,
  options = {},
  read_progress_callback = noop,
) {
  let total_bytes;

  const url = new URL(uri, 'file:///');
  switch (url.protocol) {
    case 'http:':
    case 'https:': {
      return new Promise((resolve, reject) => {
        const http_options = {
          url: uri,
        };
        if (options.range) {
          http_options.headers = {
            Range: `bytes=${options.range.start}-${options.range.end}`,
          };
        }

        const req = request(http_options);
        const read_stream = new CountingStream();
        read_stream.rlDestroy = () => req.abort();
        req.on('error', (error) => reject(error));
        read_stream.on('error', (err) => reject(err));
        req.on('response', (response) => {
          if (response.statusCode >= 200 && response.statusCode < 300) {
            total_bytes = parseInt(response.headers['content-length'], 10);
            read_stream.setExpectedBytes(total_bytes);
            resolve(read_stream);
            return read_stream.unstop();
          }
          reject(new Error('BadStatusCode'));
          return req.once('complete', ({headers, statusCode}, body) => {
            log.info('error getting uri', uri, 'got status code', statusCode);
            log.info('--response headers', headers);
            log.info('--response body:', body);
          });
        });
        read_stream.on('progress', (progress) => {
          read_progress_callback({
            step: progress.bytes_out,
            total: total_bytes,
          });
        });
        req.pipe(read_stream);
        read_stream.on('end', () => {
          const progress = read_stream.getProgress();
          read_progress_callback({
            step: progress.bytes_out,
            total: total_bytes,
          });
        });
        return null;
      });
    }
    case 'file:': {
      uri = decodeURIComponent(url.pathname); // normalized path
      await pfs.access(uri, R_OK); // this throws if access is denied
      const fs_options = {};
      if (options.range) {
        fs_options.start = options.range.start;
        fs_options.end = options.range.end;
      }
      const stats = await pfs.stat(uri);
      total_bytes = stats.size;
      const read_stream = fs.createReadStream(uri, fs_options);
      const pass_through = new CountingStream();
      pass_through.unstop();
      pass_through.rlDestroy = () => read_stream.destroy();
      pass_through.setExpectedBytes(total_bytes);
      pass_through.on('progress', (progress) => {
        read_progress_callback({
          step: progress.bytes_out,
          total: total_bytes,
        });
      });
      read_stream.pipe(pass_through);
      return pass_through;
    }
    default:
      log.error('unhandled stream protocol:', url.protocol);
      throw new Error('UnknownProtocol');
  }
};

ResourceLib.getBufferFromStream = async function (source_stream) {
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

    source_stream.on('data', bufferedRead);
    source_stream.on('error', bufferError);
    source_stream.on('end', bufferEnd);
    cleanup = function () {
      source_stream.off('data', bufferedRead);
      source_stream.off('error', bufferError);
      source_stream.off('end', bufferEnd);
    };
  });
};

ResourceLib.getBufferFromUri = async function (uri, options) {
  options = options || {};
  let num_tries = 0;
  const max_attempts = options.max_attempts || 5;
  while (true) {
    let buffer;
    try {
      const read_stream = await ResourceLib.getReadStreamByUri(uri, options);
      buffer = await ResourceLib.getBufferFromStream(read_stream);
    } catch (err) {
      if (
        err.message === 'BadStatusCode' ||
        err.code === 'TimeoutError' ||
        err.code === 'NetworkingError'
      ) {
        log.info('retryable error while getting buffer from stream', err.code);
        num_tries++;
        if (num_tries >= max_attempts) {
          log.info('too many errors getting buffer from stream');
          throw err;
        }
        await utils.sleep(Math.round(Math.exp(num_tries)) * 1000);
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
        const pass_through = new CountingStream();
        pass_through.unstop(); // for write streams we immediately unstop.
        const write_stream = fs.createWriteStream(uri);
        pass_through.pipe(write_stream);
        resolve(pass_through);

        write_stream.on('close', () => {
          pass_through.emit('rl_done');
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
  const write_stream = await ResourceLib.getWriteStreamByUri(uri, out);

  return new Promise((resolve, reject) => {
    write_stream.on('error', (err) => {
      if (uri.startsWith('s3:') && out.req) {
        log.error('error writing buffer to s3:', uri, err);
        if (
          out.req.singlePart &&
          out.req.singlePart.response &&
          out.req.singlePart.response.httpResponse
        ) {
          log.error(
            'response headers for',
            uri,
            out.req.singlePart.response.httpResponse.headers,
          );
          log.error(
            'response body for',
            uri,
            out.req.singlePart.response.httpResponse.body.toString('utf8'),
          );
        }
      }
      return reject(err);
    });

    write_stream.on('rl_done', () => {
      const progress = write_stream.getProgress();
      resolve(progress.bytes_out);
    });

    write_stream.end(buffer);
  });
};

ResourceLib.writeStringToUri = async function (str, uri) {
  return ResourceLib.writeBufferToUri(Buffer.from(str), uri);
};

ResourceLib.copyFromStreamToUri = async function (
  source_stream,
  dest_uri,
  write_progress_callback = noop,
  timeout_ms = DEFAULT_TIMEOUT_MS,
) {
  let cleanup;
  let timeout_id;
  const timeout_error = new Error('Timeout');
  timeout_error.code = 'TimeoutError';

  const out = {};
  const dest_stream = await ResourceLib.getWriteStreamByUri(dest_uri, out);

  return new Promise((resolve, reject) => {
    let done = function (err) {
      cleanup();
      if (err) {
        return reject(err);
      }
      const progress = dest_stream.getProgress();
      return resolve(progress.bytes_out);
    };
    const error = function (err) {
      cleanup();
      if (this === source_stream) {
        log.error(
          'error copying file from source_stream to uri:',
          dest_uri,
          err,
        );
        return reject(err);
      }
      log.info('error copying file to dest_stream:', dest_uri, err);
      if (dest_uri.startsWith('s3:') && out.req) {
        log.error('error copying stream to s3:', err);
        if (
          out.req.singlePart &&
          out.req.singlePart.response &&
          out.req.singlePart.response.httpResponse
        ) {
          log.error(
            'response headers for',
            dest_uri,
            out.req.singlePart.response.httpResponse.headers,
          );
          log.error(
            'response body for',
            dest_uri,
            out.req.singlePart.response.httpResponse.body.toString('utf8'),
          );
        }
      }
      return reject(err);
    };
    cleanup = function () {
      done = noop;
      if (timeout_id) {
        clearTimeout(timeout_id);
      }
      if (source_stream.off) {
        source_stream.off('error', error);
      }
      if (dest_stream.off) {
        dest_stream.off('error', error);
      }
    };
    source_stream.on('error', error);
    dest_stream.on('error', error);
    dest_stream.on('rl_done', done);

    dest_stream.on('progress', (progress) => {
      if (timeout_id) {
        clearTimeout(timeout_id);
        timeout_id = setTimeout(error, timeout_ms, timeout_error);
      }
      let expected_bytes = progress.expected_bytes;
      if (!expected_bytes && source_stream.getExpectedBytes) {
        expected_bytes = source_stream.getExpectedBytes();
        dest_stream.setExpectedBytes(expected_bytes);
      }

      write_progress_callback({
        step: progress.bytes_out,
        total: expected_bytes,
      });
    });
    dest_stream.on('end', () => {
      if (timeout_id) {
        clearTimeout(timeout_id);
        timeout_id = null;
      }
      const progress = dest_stream.getProgress();
      write_progress_callback({
        step: progress.bytes_out,
        total: progress.expected_bytes,
      });
    });

    if (timeout_ms !== 0) {
      timeout_id = setTimeout(error, timeout_ms, timeout_error);
    }
    source_stream.pipe(dest_stream);
  });
};

ResourceLib.copyByUris = async function (
  source_uri,
  dest_uri,
  read_progress_callback = noop,
  write_progress_callback = noop,
  timeout_ms = DEFAULT_TIMEOUT_MS,
) {
  // see if we have an optimized implementation
  const source_url = new URL(source_uri, 'file:///');
  const dest_url = new URL(dest_uri, 'file:///');

  const source_stream = await ResourceLib.getReadStreamByUri(
    source_uri,
    {},
    read_progress_callback,
  );
  return ResourceLib.copyFromStreamToUri(
    source_stream,
    dest_uri,
    write_progress_callback,
    timeout_ms,
  );
};

ResourceLib.copyByUrisWithRetries = async function (
  source_uri,
  dest_uri,
  read_progress_callback = noop,
  write_progress_callback = noop,
  timeout_ms = DEFAULT_TIMEOUT_MS,
  max_attempts = 5,
) {
  return utils.callWithRetries(
    () =>
      ResourceLib.copyByUris(
        source_uri,
        dest_uri,
        read_progress_callback,
        write_progress_callback,
        timeout_ms,
      ),
    max_attempts,
  );
};

ResourceLib.getDirFilenames = async function (
  uri,
  regex = /.+/u,
  options = {},
) {
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

ResourceLib.getDirSubdirNames = async function (
  uri,
  regex = /.+/u,
  options = {},
) {
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
ResourceLib.getDirFilenamesTree = async function (
  uri,
  regex = /.+/u,
  options = {},
) {
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

ResourceLib.getDirFullFilenamesTree = async function (
  uri,
  regex = /.+/u,
  options = {},
) {
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
      await pfs.mkdir(path.dirname(uri), {recursive: true});
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
