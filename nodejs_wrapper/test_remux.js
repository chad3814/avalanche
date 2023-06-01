/**
 * (c) Chad Walker, Chris Kirmse
 */

import log from '../log.js';

import Avalanche from '../avalanche.js';
import ResourceIo from '../resource_io.js';

const main = async function () {
  if (process.argv.length !== 4) {
    log.info('usage: test_remux.js <source_filename> <dest_filename>');
    return;
  }

  log.info('lavf version', Avalanche.getAvFormatVersionString());

  const sourceUri = process.argv[2];
  const resourceIo = new ResourceIo(sourceUri);
  try {
    const videoReader = Avalanche.createVideoReader();
    await videoReader.init(resourceIo);
    const result = await videoReader.remux(process.argv[3], (step, total) => {
      log.info('progress', step, total);
    });
    log.info('result', result);
  } catch (err) {
    log.info('failed to remux', err);
    return;
  } finally {
    Avalanche.destroy();
  }
  log.info('done');
};

main();
