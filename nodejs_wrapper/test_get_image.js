/**
 * (c) Chad Walker, Chris Kirmse
 */

import { promises as pfs } from 'fs';

import log from '../log.js';

import Avalanche from '../avalanche.js';
import ResourceIo from '../resource_io.js';

const main = async function () {
  if (process.argv.length !== 5) {
    log.info('usage: test_get_image.js <video_filename> <output_image_filename_ppm> <timestamp>');
    return;
  }

  log.info('lavf version', Avalanche.getAvFormatVersionString());

  const uri = process.argv[2];
  const resourceIo = new ResourceIo(uri);
  let getImageResult;
  try {
    const videoReader = Avalanche.createVideoReader();
    await videoReader.init(resourceIo);
    getImageResult = await videoReader.getImageAtTimestamp(parseFloat(process.argv[4]));
  } catch (err) {
    log.info('failed to get image', err);
    return;
  } finally {
    Avalanche.destroy();
  }
  await pfs.writeFile(process.argv[3], getImageResult.net_image_buffer);
  log.info('done');
};

main();
