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

  const sourceUri = process.argv[2];
  const resourceIo = new ResourceIo(sourceUri);
  let clipVolumeData;
  try {
    const videoReader = Avalanche.createVideoReader();
    await videoReader.init(resourceIo);
    if (!videoReader.verifyHasAudioStream()) {
      const SILENT_DB = -91;
      clipVolumeData = {
        meanVolume: SILENT_DB,
        maxVolume: SILENT_DB,
      };
      log.info('result is silence', clipVolumeData);
      return;
    }
    clipVolumeData = await videoReader.getVolumeData((step, total) => {
      log.info('progress', step, total);
    });
  } catch (err) {
    log.info('failed to get clip volume data', err);
    return;
  } finally {
    Avalanche.destroy();
  }
  log.info('result is', clipVolumeData);
};

main();
