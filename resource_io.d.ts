/**
 * (c) Chad Walker, Chris Kirmse
 */

type Activity = {
  latest_open: {
    input: string;
    output: '<running>' | 'exception' | '' | number;
  };
  latest_close: {
    input: string;
    output: '<running>' | 'success' | 'exception' | 'error_not_open' | '';
  };
  latest_read: {
    input: [uri: string, offset: number, count: number];
    output: '<running>' | 'error_not_open' | '' | number;
  };
  total_bytes_read: number;
  data_sources: {
    [key: string]: import('./data_source').DataSourceActivity;
  };
}

export class ResourceIo {
  constructor(uri: string);
  getPrimaryUri(): string
  summarizeActivity(): Activity;
  openFile(uri: string): Promise<null | number>;
  closeFile(uri: string): void;
  readFile(uri: string, offset: number, count: number): Promise<null | Buffer>;
}
