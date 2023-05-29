/**
 * (c) Chad Walker, Chris Kirmse
 */

export type DataSourceActivity = {
  input_offset: number;
  buffers_size: number;
  is_in_data_request: boolean;
  is_fetching: boolean;
};

export default class DataSource {
  constructor(url: string);
  init(): Promise<void>;
  getUrl(): string;
  summarizeActivity(): DataSourceActivity;
  getTotalSize(): number;
  getLoadedSize(): number;
  dataRequest(offset, count): Promise<Buffer[]>;
};