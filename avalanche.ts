/**
 * (c) Chad Walker, Chris Kirmse
 */

import sem from 'semaphore-mutex';
import log from './log.js';
import ResourceIo from './resource_io.js';
import { createRequire } from 'module';
const require = createRequire(import.meta.url);

// tslint:disable:no-var-requires
const avalancheNative = require('./Release/avalanche.node');
const Semaphore = sem.default;

let pendingStr = '';

const defaultLogAvalanche = (level: string, isLibav: boolean, str: string) => {
  pendingStr += str;
  if (!pendingStr.endsWith('\n')) {
    return;
  }
  // remove newline
  pendingStr = pendingStr.slice(0, -1);

  if (isLibav) {
    log.trace('libav:', pendingStr);
  } else {
    switch (level) {
      case 'info':
        log.info('avalanche:', pendingStr);
        break;
      case 'error':
        // video errors can be caused by bad data, which is expected from some users;
        // therefore this is a log info
        log.info('avalanche:', pendingStr);
        break;
      default:
        log.info('avalanche: unknown level', level, pendingStr);
        break;
    }
  }
  pendingStr = '';
};

type LogFn = (level: string, isLibav: boolean, str: string) => void;

const setLogFunc = (logFunc: LogFn) => {
  avalancheNative.setLogFunc(logFunc);
};

const setDefaultLogFunc = () => {
  setLogFunc(defaultLogAvalanche);
};

setDefaultLogFunc();

const activeVideoReaders = new Set<LockedVideoReader>();

type Token = number;
type Action = {
  input: any[] | null;
  output: any | null;
};
type Metadata = {
  video_encoding_name: string;
  audio_encoding_name: string;

  container_start_time: number;
  container_duration: number;

  video_start_time: number;
  video_duration: number;
  video_width: number;
  video_height: number;
  frame_rate: number;
};
type ImageData = {
  net_image_buffer: Buffer;
  timestamp: number;
  duration: number;
};
type VideoData = {
  video_start_time: number;
  video_duration: number;

  count_video_packets: number;
  count_key_frames: number;
};
type VolumeData = {
  mean_volume: number;
  max_volume: number;
};
type ProgressFn = (step: number, total: number) => void;

// we wrap the calls into video read here with our own javascript mutex
// which is implemented as a "fair" mutex, meaning all the calls get to the
// native code in the same order as they get here
//
// Important note: the wrapped_video_reader and c++ VideoReader implementetation
// only allow one high-level call at a time; that's why some locking/queuing
// is required.
class LockedVideoReader {
  private _videoReader;
  private _lock = new Semaphore(1);
  private _latestAction: Action = {
    input: null,
    output: null,
  };

  constructor() {
    this._videoReader = new avalancheNative.VideoReader();
  }

  async _startAction() {
    const token = (await this._lock.acquire()) as Token;
    activeVideoReaders.add(this);
    return token;
  }

  _endAction(token: Token) {
    activeVideoReaders.delete(this);
    this._lock.release(token);
  }

  async init(input: string | typeof ResourceIo) {
    const token = await this._startAction();
    this._latestAction = {
      input: ['init', input],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._videoReader.init(input);
      this._latestAction.output = retval;
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  drain() {
    this._videoReader.drain();
  }

  async destroy() {
    const token = await this._startAction();
    this._latestAction = {
      input: ['destroy'],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._videoReader.destroy();
      this._latestAction.output = retval;
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async verifyHasVideoStream() {
    const token = await this._startAction();
    this._latestAction = {
      input: ['init'],
      output: '<running>',
    };
    let retval;
    try {
      retval = this._videoReader.verifyHasVideoStream();
      this._latestAction.output = retval;
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async verifyHasAudioStream() {
    const token = await this._startAction();
    this._latestAction = {
      input: ['init'],
      output: '<running>',
    };
    let retval;
    try {
      retval = this._videoReader.verifyHasAudioStream();
      this._latestAction.output = retval;
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async getMetadata(): Promise<Metadata> {
    const token = await this._startAction();
    this._latestAction = {
      input: ['get_metadata'],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._videoReader.getMetadata();
      this._latestAction.output = retval;
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async getImageAtTimestamp(timestamp: number): Promise<ImageData> {
    const token = await this._startAction();
    this._latestAction = {
      input: ['get_image_at_timestamp', timestamp],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._videoReader.getImageAtTimestamp(timestamp);
      this._latestAction.output = { ...retval };
      if (this._latestAction.output.net_image_buffer) {
        this._latestAction.output.net_image_buffer = `Buffer length ${this._latestAction.output.net_image_buffer.length}`;
      }
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async extractClipReencode(
    destUri: string,
    startTime: number,
    endTime: number,
    progress: ProgressFn,
  ): Promise<VideoData> {
    const token = await this._startAction();
    this._latestAction = {
      input: ['extract_clip_reencode', destUri, startTime, endTime],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._videoReader.extractClipReencode(destUri, startTime, endTime, progress);
      this._latestAction.output = retval;
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async extractClipRemux(
    destUri: string,
    startTime: number,
    endTime: number,
    progress: ProgressFn,
  ): Promise<VideoData> {
    const token = await this._startAction();
    this._latestAction = {
      input: ['extract_clip_remux', destUri, startTime, endTime],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._videoReader.extractClipRemux(destUri, startTime, endTime, progress);
      this._latestAction.output = retval;
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async remux(destUri: string, progress: ProgressFn): Promise<VideoData> {
    const token = await this._startAction();
    this._latestAction = {
      input: ['remux', destUri],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._videoReader.remux(destUri, progress);
      this._latestAction.output = retval;
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async getClipVolumeData(startTime: number, endTime: number, progress: ProgressFn): Promise<VolumeData> {
    const token = await this._startAction();
    this._latestAction = {
      input: ['get_clip_volume_data', startTime, endTime],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._videoReader.getClipVolumeData(startTime, endTime, progress);
      this._latestAction.output = retval;
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  async getVolumeData(progress: ProgressFn): Promise<VolumeData> {
    const token = await this._startAction();
    this._latestAction = {
      input: ['get_volume_data'],
      output: '<running>',
    };
    let retval;
    try {
      retval = await this._videoReader.getVolumeData(progress);
      this._latestAction.output = retval;
    } catch (err) {
      this._latestAction.output = 'exception';
      throw err;
    } finally {
      this._endAction(token);
    }
    return retval;
  }

  getLatestAction() {
    return this._latestAction;
  }

  getBlockedCount() {
    return this._lock.getBlockedCount();
  }
}

const createVideoReader = () => {
  return new LockedVideoReader();
};

const drainActiveVideoReaders = () => {
  for (const videoReader of activeVideoReaders) {
    // log this because it's not very common
    // also it is helpful to know in case things go wrong
    log.info('draining a video reader');
    videoReader.drain();
  }
  activeVideoReaders.clear();
};

process.on('exit', (code) => {
  // other threads can be waiting on a uv_cond that is waiting for javascript to signal them;
  // since the process is exiting, no more javascript will be run, so those threads will be stuck
  // and the process won't actually exit. Therefore, we tell all running video_readers
  // to unstick everything (aka drain) which causes all other threads to finish their operations
  drainActiveVideoReaders();
});

export default {
  ...avalancheNative,
  defaultLogAvalanche,
  setLogFunc,
  createVideoReader,
};
