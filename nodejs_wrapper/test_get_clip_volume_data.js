/**
 * (c) Chad Walker, Chris Kirmse
 */

import log from '../log.js';

import Avalanche from '../avalanche.js';
import ResourceIo from '../resource_io.js';

const main = async function () {
  if (process.argv.length !== 5) {
    log.info('usage: test_get_clip_volume_data.js <source_filename> <start_time> <end_time>');
    return;
  }

  log.info('lavf version', Avalanche.getAvFormatVersionString());

  const sourceUri = process.argv[2];
  const resourceIo = new ResourceIo(sourceUri);
  let clipVolumeData;
  try {
    const videoReader = Avalanche.createVideoReader();
    await videoReader.init(resourceIo);
    clipVolumeData = await videoReader.getClipVolumeData(
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
  log.info('result is', clipVolumeData);
};

main();
