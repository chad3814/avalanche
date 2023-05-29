/**
 * (c) Chad Walker, Chris Kirmse
 */

type Metadata = {
  video_encoding_name: string;
  audio_encoding_name: string;

  container_start_time: number;
  container_duration: number;

  video_start_time: number;
  video_duration: number;
  video_width: number;
  video_height: number;
  video_rate: number;
};
type ImageData = {
  net_image_buffer: Buffer;
  timestamp: number;
  duration: number;
};
type ClipData = {
  video_start_time: number;
  video_duration: number;
  count_video_packets: number;
  count_key_frames: number;
};
type ProgressFunc = (step: number, total: number) => void;
type VolumeData = {
  mean_volume: number;
  max_volume: number;
};
type ActionData = {
  input: any[];
  output: '<Running>' | 'exception' | boolean | VolumeData | ClipData | ImageData | Metadata;
};
export class LockedVideoReader {
  constructor();
  init(uri: string | ResourceIo = import('./resource_io')): Promise<boolean>;
  drain(): void;
  destroy(): Promise<null>;
  verifyHasVideoStream(): Promise<boolean>;
  verifyHasAudioStream(): Promise<boolean>;
  getMetadata(): Promise<Metadata>;
  getImageAtTimestamp(timestamp: number): Promise<ImageData>;
  extractClipReencode(dest_uri: string, start_time: number, end_time: number, progress: ProgressFunc): Promise<ClipData>;
  extractClipRemux(dest_uri: string, start_time: number, end_time: number, progress: ProgressFunc): Promise<ClipData>;
  remux(dest_uri: string, progress: ProgressFunc): Promise<ClipData>;
  getClipVolumeData(start_time: number, end_time: number, progress: ProgressFunc): Promise<VolumeData>;
  getVolumeData(progress: ProgressFunc): Promise<VolumeData>;
  getLatestAction(): ActionData;
  getBlockedCount(): number;
}

type logFn = (level: 'info' | 'error' | 'warn' | 'debug', is_libav: boolean, str: string) => void;
type _export = {
  createVideoReader: () => LockedVideoReader;
  defaultLogAvalanche: logFn;
  setLogFunc: (log_func: logFn) => void;
};
export default _export;
