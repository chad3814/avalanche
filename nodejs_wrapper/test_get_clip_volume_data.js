/**
 * (c) Chad Walker, Chris Kirmse
 */

import log from '../log.js';

import Avalanche from '../avalanche.js';
import ResourceIo from '../resource_io.js';

const main = async function () {
  if (process.argv.length !== 5) {
    log.info(
      'usage: test_get_clip_volume_data.js <source_filename> <start_time> <end_time>',
    );
    return;
  }

  log.info('lavf version', Avalanche.getAvFormatVersionString());

  const source_uri = process.argv[2];
  const resource_io = new ResourceIo(source_uri);
  let clip_volume_data;
  try {
    const video_reader = Avalanche.createVideoReader();
    await video_reader.init(resource_io);
    clip_volume_data = await video_reader.getClipVolumeData(
      parseFloat(process.argv[3]),
      parseFloat(process.argv[4]),
      (step, total) => {
        log.info('progress', step, total);
      },
    );
  } catch (err) {
    log.info('failed to get clip volume data', err);
    return;
  } finally {
    Avalanche.destroy();
  }
  log.info('result is', clip_volume_data);
};

main();
