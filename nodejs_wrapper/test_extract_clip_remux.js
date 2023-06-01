/**
 * (c) Chad Walker, Chris Kirmse
 */

import log from '../log.js';

import Avalanche from '../avalanche.js';
import ResourceIo from '../resource_io.js';

const main = async function () {
  if (process.argv.length !== 6) {
    log.info('usage: test_extract_clip_remux.js <source_filename> <dest_filename> <start_time> <end_time>');
    return;
  }

  log.info('lavf version', Avalanche.getAvFormatVersionString());

  const sourceUri = process.argv[2];
  const resourceIo = new ResourceIo(sourceUri);
  try {
    const videoReader = Avalanche.createVideoReader();
    await videoReader.init(resourceIo);
    const result = await videoReader.extractClipRemux(
      process.argv[3],
      parseFloat(process.argv[4]),
      parseFloat(process.argv[5]),
      (step, total) => {
        log.info('progress', step, total);
      },
    );
    log.info('result', result);
  } catch (err) {
    log.info('failed to extract clip remux', err);
    return;
  } finally {
    Avalanche.destroy();
  }
  log.info('done');
};

main();
