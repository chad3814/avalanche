/**
 * (c) Chad Walker, Chris Kirmse
 */

import os from 'os';
import path from 'path';
import url from 'url';

import { Mutex } from 'semaphore-mutex';

import log from './log.js';

const __dirname = path.dirname(url.fileURLToPath(import.meta.url));

// es modules can't import native code, so the nodejs docs say to do this instead
const avalanche_native_module = {exports: {}};
process.dlopen(
  avalanche_native_module,
  path.join(__dirname, 'build/Release/avalanche.node'),
  os.constants.dlopen.RTLD_NOW,
);
const avalanche_native = avalanche_native_module.exports;

let pending_str = '';

const defaultLogAvalanche = function (level, is_libav, str) {
  pending_str += str;
  if (!pending_str.endsWith('\n')) {
    return;
  }
  // remove newline
  pending_str = pending_str.slice(0, -1);

  if (is_libav) {
    log.trace('libav:', pending_str);
  } else {
    switch (level) {
      case 'info':
        log.info('avalanche:', pending_str);
        break;
      case 'error':
        // video errors can be caused by bad data, which is expected from some users;
        // therefore this is a log info
        log.info('avalanche:', pending_str);
        break;
      default:
        log.info('avalanche: unknown level', level, pending_str);
        break;
    }
  }
  pending_str = '';
};

const setLogFunc = function (log_func) {
  avalanche_native.setLogFunc(log_func);
};

const setDefaultLogFunc = function (log_func) {
  setLogFunc(defaultLogAvalanche);
};

setDefaultLogFunc();

const active_video_readers = new Set();

// we wrap the calls into video read here with our own javascript mutex
// which is implemented as a "fair" mutex, meaning all the calls get to the
// native code in the same order as they get here
//
// Important note: the wrapped_video_reader and c++ VideoReader implementetation
// only allow one high-level call at a time; that's why some locking/queuing
// is required.
class LockedVideoReader {
  constructor(...args) {
    this._video_reader = new avalanche_native.VideoReader(...args);
    this._lock = new Mutex();
  }

  async _startAction() {
    const token = await this._lock.acquire();
    active_video_readers.add(this);
    return token;
  }

  _endAction(token) {
    active_video_readers.delete(this);
    this._lock.release(token);
  }

  async init(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['init', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._video_reader.init(...args);
      this._latest_action.output = retval;
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  drain() {
    this._video_reader.drain();
  }

  async destroy(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['destroy', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._video_reader.destroy(...args);
      this._latest_action.output = retval;
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async verifyHasVideoStream(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['init', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = this._video_reader.verifyHasVideoStream(...args);
      this._latest_action.output = retval;
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async verifyHasAudioStream(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['init', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = this._video_reader.verifyHasAudioStream(...args);
      this._latest_action.output = retval;
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async getMetadata(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['get_metadata', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._video_reader.getMetadata(...args);
      this._latest_action.output = retval;
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async getImageAtTimestamp(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['get_image_at_timestamp', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._video_reader.getImageAtTimestamp(...args);
      this._latest_action.output = {...retval};
      if (this._latest_action.output.net_image_buffer) {
        this._latest_action.output.net_image_buffer = `Buffer length ${this._latest_action.output.net_image_buffer.length}`;
      }
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async extractClipReencode(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['extract_clip_reencode', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._video_reader.extractClipReencode(...args);
      this._latest_action.output = retval;
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async extractClipRemux(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['extract_clip_remux', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._video_reader.extractClipRemux(...args);
      this._latest_action.output = retval;
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async remux(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['remux', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._video_reader.remux(...args);
      this._latest_action.output = retval;
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async getClipVolumeData(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['get_clip_volume_data', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._video_reader.getClipVolumeData(...args);
      this._latest_action.output = retval;
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async getVolumeData(...args) {
    const token = await this._startAction();
    this._latest_action = {
      input: ['get_volume_data', ...args],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._video_reader.getVolumeData(...args);
      this._latest_action.output = retval;
    } catch (err) {
      this._latest_action.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  getLatestAction() {
    return this._latest_action;
  }

  getBlockedCount() {
    return this._lock.getBlockedCount();
  }
}

const createVideoReader = function (...args) {
  return new LockedVideoReader(...args);
};

const drainActiveVideoReaders = function () {
  for (const video_reader of active_video_readers) {
    // log this because it's not very common
    // also it is helpful to know in case things go wrong
    log.info('draining a video reader');
    video_reader.drain();
  }
  active_video_readers.clear();
};

process.on('exit', (code) => {
  // other threads can be waiting on a uv_cond that is waiting for javascript to signal them;
  // since the process is exiting, no more javascript will be run, so those threads will be stuck
  // and the process won't actually exit. Therefore, we tell all running video_readers
  // to unstick everything (aka drain) which causes all other threads to finish their operations
  drainActiveVideoReaders();
});

export default {
  ...avalanche_native,
  defaultLogAvalanche,
  setLogFunc,
  createVideoReader,
};
