/**
 * (c) Chad Walker, Chris Kirmse
 */

import { promises as pfs } from 'fs';

import log from '../log.js';
import utils from '../../utils.js';

import Avalanche from '../avalanche.js';
import ResourceIo from '../resource_io.js';

const main = async function () {
  if (process.argv.length !== 7) {
    log.info(
      'usage: test_get_multiple_images.js <video_filename> <filename template> <start_timestamp> <end_timestamp> <gap_time>',
    );
    return;
  }

  const uri = process.argv[2];
  const resourceIo = new ResourceIo(uri);
  const destFilenameTemplate = process.argv[3];
  const startTime = parseFloat(process.argv[4]);
  const endTime = parseFloat(process.argv[5]);
  const gapTime = parseFloat(process.argv[6]);
  try {
    const videoReader = Avalanche.createVideoReader();
    await videoReader.init(resourceIo);

    let timestamp = startTime;
    while (timestamp < endTime) {
      const getImageResult = await videoReader.getImageAtTimestamp(timestamp);
      log.info(
        `asked for image at timestamp ${timestamp} actually got ${getImageResult.timestamp} duration ${getImageResult.duration}`,
      );

      timestamp = getImageResult.timestamp + gapTime;
      await pfs.writeFile(
        `${destFilenameTemplate}${utils.roundToDecimalDigits(getImageResult.timestamp, 3)}.ppm`,
        getImageResult.net_image_buffer,
      );
    }
  } catch (err) {
    log.info('failed video reader', err);
    return;
  } finally {
    Avalanche.destroy();
  }
};

main();
