/**
 * (c) Chad Walker, Chris Kirmse
 */

import log from '../log.js';

import Avalanche from '../avalanche.js';
import ResourceIo from '../resource_io.js';

const main = async function () {
  if (process.argv.length !== 3) {
    log.info('usage: test_get_metadata.js <video_filename>');
    return;
  }

  log.info('lavf version', Avalanche.getAvFormatVersionString());

  const uri = process.argv[2];
  const resourceIo = new ResourceIo(uri);
  let metadata;
  try {
    const videoReader = Avalanche.createVideoReader();
    await videoReader.init(resourceIo);
    metadata = await videoReader.getMetadata();
  } catch (err) {
    return;
  } finally {
    Avalanche.destroy();
  }
  log.info('result is', metadata);
};

main();
