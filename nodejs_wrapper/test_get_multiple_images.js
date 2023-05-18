/**
 * (c) Chad Walker, Chris Kirmse
 */

import {promises as pfs} from 'fs';

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
  const resource_io = new ResourceIo(uri);
  const dest_filename_template = process.argv[3];
  const start_time = parseFloat(process.argv[4]);
  const end_time = parseFloat(process.argv[5]);
  const gap_time = parseFloat(process.argv[6]);
  try {
    const video_reader = Avalanche.createVideoReader();
    await video_reader.init(resource_io);

    let timestamp = start_time;
    while (timestamp < end_time) {
      const get_image_result = await video_reader.getImageAtTimestamp(
        timestamp,
      );
      log.info(
        `asked for image at timestamp ${timestamp} actually got ${get_image_result.timestamp} duration ${get_image_result.duration}`,
      );

      timestamp = get_image_result.timestamp + gap_time;
      await pfs.writeFile(
        `${dest_filename_template}${utils.roundToDecimalDigits(
          get_image_result.timestamp,
          3,
        )}.ppm`,
        get_image_result.net_image_buffer,
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
