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
  const resource_io = new ResourceIo(uri);
  let metadata;
  try {
    const video_reader = Avalanche.createVideoReader();
    await video_reader.init(resource_io);
    metadata = await video_reader.getMetadata();
  } catch (err) {
    return;
  } finally {
    Avalanche.destroy();
  }
  log.info('result is', metadata);
};

main();
