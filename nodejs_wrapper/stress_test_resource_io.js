/**
 * (c) Chad Walker, Chris Kirmse
 */

import log from '../log.js';

import Avalanche from '../avalanche.js';
import ResourceIo from '../resource_io.js';
/*
class ResourceIo {
  getPrimaryUri() {
    return 'test_uri.mp4';
  }
  async openFile(uri) {
    // log.info('opening', uri);
    return 10;
  }
  closeFile(uri) {
    // log.info('closing', uri);
    return null;
  }
  async readFile(uri, offset, count) {
    return [Buffer.from('fred'), Buffer.from('joanna')];
  }
}
*/

const main = async function () {
  const resourceIo = new ResourceIo('/home/ckirmse/junk/new/playlist.m3u8');
  let metadata;
  try {
    metadata = await Avalanche.stressTestResourceIo(resourceIo);
  } catch (err) {
    log.info('failed stress test', err);
    return;
  } finally {
    Avalanche.destroy();
  }
  log.info('result is', metadata);
};

main();
