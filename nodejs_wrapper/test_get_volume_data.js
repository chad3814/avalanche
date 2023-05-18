/**
 * (c) Chad Walker, Chris Kirmse
 */

import log from '../log.js';

import Avalanche from '../avalanche.js';
import ResourceIo from '../resource_io.js';

const main = async function () {
  if (process.argv.length !== 3) {
    log.info('usage: test_get_clip_volume_data.js <source_filename>');
    return;
  }

  log.info('lavf version', Avalanche.getAvFormatVersionString());

  const source_uri = process.argv[2];
  const resource_io = new ResourceIo(source_uri);
  let clip_volume_data;
  try {
    const video_reader = Avalanche.createVideoReader();
    await video_reader.init(resource_io);
    if (!video_reader.verifyHasAudioStream()) {
      const SILENT_DB = -91;
      clip_volume_data = {
        mean_volume: SILENT_DB,
        max_volume: SILENT_DB,
      };
      log.info('result is silence', clip_volume_data);
      return;
    }
    clip_volume_data = await video_reader.getVolumeData((step, total) => {
      log.info('progress', step, total);
    });
  } catch (err) {
    log.info('failed to get clip volume data', err);
    return;
  } finally {
    Avalanche.destroy();
  }
  log.info('result is', clip_volume_data);
};

main();
