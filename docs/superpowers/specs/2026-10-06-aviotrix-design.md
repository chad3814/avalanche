# aviotrix design: libav core with Node native and WASM bindings

Date: 2026-10-06
Status: approved design, awaiting implementation plan
Successor to: `avalanche` / `avalanche-video` (Athenascope-era N-API module, pre-reset tree at `0437a55`)

## 1. Goal and scope

Rebuild the `avalanche` libav bindings as **aviotrix**: a single C++ core that wraps
libavformat/libavcodec behind a custom AVIO abstraction, exposed as both a Node
native module and a WebAssembly module for browsers.

### Milestone 1 (this spec)

The thinnest vertical slice that proves the shared core, both bindings, and the
sync/async bridge on each target:

1. Open a media source through a host-supplied async `IoSource`.
2. Report container and stream metadata.
3. Remux (stream copy, no transcoding) to a host-supplied async `IoSink`.

Works identically on Node and in a JSPI-capable browser.

### Explicitly out of scope for milestone 1

- Frame extraction, clip re-encode, volume analysis (the rest of the old
  `avalanche` feature set). Each gets its own spec later.
- Any decoding or encoding, including subtitle format conversion.
- A sync fallback for browsers without JSPI.
- Windows prebuilt binaries.
- libopenshot / libopenshot-audio (long-term goal; the core boundaries must not
  make it painful, but nothing is built for it now).

### Non-goals

- Replacing libav.js, transavormer, or Mediabunny for people who just want a
  browser remuxer. aviotrix exists for the AVIO abstraction and the shared core
  that also backs a native module.

## 2. Naming and distribution

- Project and GitHub repo: `aviotrix/aviotrix` (org owned by Chad).
- npm scope `@aviotrix` (org owned by Chad). Packages:
  - `@aviotrix/types` — TypeScript-only shared interfaces and types.
  - `@aviotrix/node` — N-API binding, TS wrapper, prebuilt binaries.
  - `@aviotrix/wasm` — Emscripten binding, TS wrapper, `.wasm` + glue.
- License: MIT for aviotrix. libav is built LGPL-only (no `--enable-gpl`,
  no `--enable-nonfree`) so the WASM can ship in a webpage.

## 3. Architecture

Three layers with one direction of dependency. The core knows nothing about
Node or the browser. Each binding knows the core and its own runtime. Each TS
package knows only its binding plus `@aviotrix/types`.

```
aviotrix/                       bare-repo + worktrees layout
  third_party/ffmpeg/           git submodule pinned to one FFmpeg 8.x tag
  scripts/build-ffmpeg.sh       one configure recipe; `host` and `wasm` targets
  scripts/make-fixtures.sh      regenerates fixtures/ with a system ffmpeg (dev only)
  fixtures/                     small committed media files for tests
  core/                         C++20 static lib; includes only libav headers
    include/aviotrix/...
    src/
    tests/                      CTest, file-backed IoSource/IoSink
    CMakeLists.txt
  packages/
    types/                      @aviotrix/types
    node/                       @aviotrix/node: binding/ (C++), src/ (TS), tests/
    wasm/                       @aviotrix/wasm: binding/ (C++), src/ (TS), tests/
  CMakeLists.txt                top level: ffmpeg external step, core, both bindings
  package.json                  npm workspaces root
```

### 3.1 Core (`core/`)

Synchronous C++20, no exceptions, no Node or Emscripten headers.

Abstract IO interfaces (positioned, so hosts never track stream position):

```cpp
class IoSource {
 public:
  virtual ~IoSource() = default;
  // Returns total size in bytes, or std::nullopt if unknown (=> unseekable).
  virtual Status open(std::optional<int64_t>& size) = 0;
  // Reads up to buf.size() bytes at offset. Short reads allowed. 0 bytes = EOF.
  virtual Status read(int64_t offset, std::span<uint8_t> buf, size_t& bytesRead) = 0;
  virtual Status close() = 0;
};

class IoSink {
 public:
  virtual ~IoSink() = default;
  virtual bool seekable() const = 0;
  virtual Status open() = 0;
  virtual Status write(int64_t offset, std::span<const uint8_t> data) = 0;
  virtual Status close() = 0;
};
```

The core owns the `AVIOContext`s. It adapts libav's sequential
`read_packet`/`seek` callbacks onto the positioned interface by tracking the
current offset itself. A `std::nullopt` size marks the input context unseekable.

Operations:

- `MediaReader::open(IoSource&, OpenOptions) -> Status` then `metadata()`.
- `MediaReader::remux(IoSink&, RemuxOptions, RemuxResult&) -> Status`.
- `MediaReader::close()`.

`Status` carries a libav error code (`AVERROR_*`) or an aviotrix code, plus a
message. Success is code 0. libav's log callback is forwarded to an optional
host `LogHook(level, text)`.

Cancellation: `RemuxOptions` holds a pointer to an atomic cancel flag. The
packet loop checks it each iteration; IO callbacks return `AVERROR_EXIT` when
set.

### 3.2 Node binding (`packages/node/binding/`)

node-addon-api, built with cmake-js.

- Each `MediaReader` owns one dedicated `std::thread`. A JS call posts work to
  that thread and returns a Promise.
- When the core needs IO, the thread calls a `Napi::ThreadSafeFunction`, which
  invokes the JS `IoSource.read` / `IoSink.write` on the main thread, and waits
  on a condition variable until the Promise settles and the bytes are copied
  into the core's buffer. Exactly one outstanding IO per reader by construction.
- The TS wrapper serializes calls on one reader (fair queue), so the C++ side
  never sees concurrent calls. Equivalent to the old `LockedVideoReader`.
- libav links statically into the `.node`; consumers have no runtime libav
  dependency.

### 3.3 WASM binding (`packages/wasm/binding/`)

Emscripten.

- JSPI enabled; the IO import functions are declared suspending, so the
  synchronous core call pauses until the JS Promise settles.
- No pthreads, so no COOP/COEP requirements. Runs on the main thread or in a
  Worker.
- Flags: memory growth on, ES module output, `web,worker` environments, asm
  off, optimize for size first; measured size recorded in the README.
- If `WebAssembly.Suspending` is missing, `load()` rejects with
  `AviotrixError` code `UNSUPPORTED_RUNTIME`.

## 4. Shared TypeScript contract (`@aviotrix/types`)

```ts
export type MaybePromise<T> = T | Promise<T>;

export interface IoSource {
  open(): MaybePromise<number | null>;            // total size, or null if unknown
  read(offset: number, length: number): MaybePromise<Uint8Array>; // short read ok; empty = EOF
  close(): MaybePromise<void>;
}

export interface IoSink {
  readonly seekable: boolean;
  open(): MaybePromise<void>;
  write(offset: number, data: Uint8Array): MaybePromise<void>;
  close(): MaybePromise<void>;
}

export type LogLevel = 'quiet' | 'panic' | 'fatal' | 'error' | 'warning' | 'info' | 'verbose' | 'debug' | 'trace';
export type LogFn = (level: LogLevel, text: string) => void;

export interface OpenOptions {
  onLog?: LogFn;
}

export interface Rational { num: number; den: number; }

export interface VideoStreamInfo {
  width: number;
  height: number;
  frameRate: Rational | null;
  pixelFormat: string | null;
}

export interface AudioStreamInfo {
  sampleRate: number;
  channels: number;
  channelLayout: string | null;
}

export interface StreamInfo {
  index: number;
  type: 'video' | 'audio' | 'subtitle' | 'data' | 'attachment';
  codec: string;              // libav codec name, e.g. "h264"
  codecTag: string | null;
  timeBase: Rational;
  startTime: number | null;   // seconds
  duration: number | null;    // seconds
  bitRate: number | null;
  language: string | null;
  tags: Record<string, string>;
  video?: VideoStreamInfo;
  audio?: AudioStreamInfo;
}

export interface Metadata {
  format: string;             // libav input format name
  formatLongName: string;
  startTime: number | null;
  duration: number | null;
  bitRate: number | null;
  tags: Record<string, string>;
  streams: StreamInfo[];
}

export interface RemuxProgress {
  bytesRead: number;
  bytesWritten: number;
  timestamp: number | null;   // latest muxed timestamp, seconds
}

export interface RemuxOptions {
  format: string;                               // output container, e.g. "mp4", "matroska", "mpegts"
  streams?: number[];                           // input stream indices; default all
  onIncompatibleStream?: 'skip' | 'fail';       // default 'skip'
  fragmented?: boolean;                         // MP4 frag_keyframe+empty_moov; required if !sink.seekable
  signal?: AbortSignal;
  onProgress?: (progress: RemuxProgress) => void;
}

export interface RemuxStreamMapping {
  input: number;
  output: number | null;
  skippedReason?: string;
}

export interface RemuxResult {
  streams: RemuxStreamMapping[];
  bytesRead: number;
  bytesWritten: number;
  packets: number;
}

export class AviotrixError extends Error {
  readonly code: string;      // e.g. "AVERROR_INVALIDDATA", "UNSUPPORTED_RUNTIME", "ABORTED"
}
```

Rules:

- Fields libav does not know are `null`, never a guessed zero.
- No `any`, no `unknown` anywhere in the public or internal TS.

## 5. Public API (identical on both packages)

```ts
export class MediaReader {
  static open(source: IoSource, options?: OpenOptions): Promise<MediaReader>;
  readonly metadata: Metadata;
  remux(sink: IoSink, options: RemuxOptions): Promise<RemuxResult>;
  close(): Promise<void>;
  [Symbol.asyncDispose](): Promise<void>;
}

export function readMetadata(source: IoSource, options?: OpenOptions): Promise<Metadata>;
export function remux(source: IoSource, sink: IoSink, options: RemuxOptions): Promise<RemuxResult>;
```

`@aviotrix/wasm` additionally exports `load(options?: { wasmUrl?: string }): Promise<void>`;
`MediaReader.open` awaits it implicitly if not already called.

Behavior:

- One operation at a time per `MediaReader`; the TS wrapper queues calls.
- `remux` with a non-seekable sink and `format: 'mp4'` without `fragmented: true`
  rejects before any IO with code `SINK_NOT_SEEKABLE`.
- With `onIncompatibleStream: 'skip'` (default), streams the target muxer
  rejects are dropped, a warning is emitted via `onLog`, and the mapping shows
  `output: null` with `skippedReason`. With `'fail'`, `remux` rejects with
  `INCOMPATIBLE_STREAM`.
- Aborting via `signal` rejects `remux` with code `ABORTED`; the sink is still
  closed.

Reference IO implementations shipped per package:

- Node: `FileSource(path)`, `FileSink(path)` (seekable), `MemorySink()`.
- WASM: `BlobSource(file: Blob)`, `FetchRangeSource(url)` (HTTP range requests),
  `MemorySink()` (seekable, exposes `toBlob()` / `bytes()`).

## 6. FFmpeg build

One recipe in `scripts/build-ffmpeg.sh`, parameterized by target (`host` or
`wasm`), producing static libs in `build/ffmpeg-<target>/`. Pinned to the latest
stable FFmpeg 8.x tag at implementation time; the tag is recorded in the
submodule and in `README.md`.

Enabled:

- Libraries: `avformat`, `avcodec`, `avutil`.
- Demuxers: `mov`, `matroska`, `mpegts`.
- Muxers: `mp4`, `mov`, `matroska`, `webm`, `mpegts`.
- Parsers: `h264`, `hevc`, `av1`, `vp9`, `aac`, `ac3`, `opus`, `vorbis`,
  `mpegaudio`, `mpegvideo`, `dvbsub`, `dvdsub`.
- Bitstream filters: `h264_mp4toannexb`, `hevc_mp4toannexb`, `aac_adtstoasc`,
  `extract_extradata`.

Disabled: everything else, including `avfilter`, `avdevice`, `swscale`,
`swresample`, `postproc`, programs, docs, network, all protocols, all
decoders, encoders, filters, and autodetected external libraries.

Why parsers for a stream-copy build: MPEG-TS demuxing needs them to split PES
payloads into access units; `avformat_find_stream_info` needs them to fill
dimensions and frame rate for TS; the MP4 muxer's auto-inserted
`extract_extradata` needs them for H.264/HEVC from Annex B; AC-3 needs its
parser for `dac3`/`dec3` boxes.

Host target: native asm on (nasm required), PIC, static. WASM target: asm off,
built under `emconfigure`.

## 7. Build system

CMake throughout.

- Top-level `CMakeLists.txt` runs `build-ffmpeg.sh` as an external step keyed
  on the toolchain, builds `core` as a static library, builds the Node binding
  when invoked by cmake-js (`CMAKE_JS_INC` present), and builds the WASM binding
  when `CMAKE_SYSTEM_NAME` is `Emscripten`.
- Separate build directories per toolchain.
- Prebuilt native binaries: produced in CI for `darwin-arm64`, `darwin-x64`,
  `linux-x64`, `linux-arm64`; attached to GitHub Releases by `prebuild`
  (cmake-js backend); fetched at install by `prebuild-install`. Binaries are
  downloaded rather than bundled because each one statically includes libav.
  If none matches, install builds from source and prints the required tools
  (cmake, make, nasm, a C++20 compiler).

## 8. Testing

Fixtures (`fixtures/`, committed, each a few seconds long):

- `h264-aac.mp4`
- `vp9-opus.webm`
- `h264-ac3.ts`
- `h264-aac-srt.mkv` (exercises the subtitle skip path)

`scripts/make-fixtures.sh` regenerates them with a system `ffmpeg`
(developer-only dependency).

Core (CTest, C++): file-backed source/sink; open each fixture and assert
metadata; remux every supported container pair; reopen output through the core
and compare stream count, codec names, duration, packet count.

Node (Vitest): `FileSource` happy path; an adversarial source with short reads
and random delays; non-seekable sink rejecting MP4 without `fragmented`; abort
mid-remux; queued concurrent calls on one reader; error mapping to
`AviotrixError`; subtitle skip vs fail.

WASM (Vitest browser mode via Playwright, Chromium): same suites against
`BlobSource` and `FetchRangeSource`; an unsupported-runtime test with
`WebAssembly.Suspending` deleted.

## 9. Tooling and CI

- npm workspaces (root `package.json`, `--workspaces` scripts).
- TypeScript 7 (the Go-based compiler) in strict mode for type-checking and
  emit. ESLint is not yet compatible with TS 7, so linting uses **oxlint** with
  its TypeScript rules: `no-explicit-any` on as an error, and `unknown` banned
  via `no-restricted-syntax` on `TSUnknownKeyword` if the installed oxlint
  supports that rule, otherwise via a small CI grep over `packages/*/src`.
  Prettier for formatting (2-space, semicolons).
- clang-format and clang-tidy for C++.
- GitHub Actions: build FFmpeg once per (recipe hash, toolchain) and cache;
  core + Node tests on macOS arm64, Linux x64, Linux arm64; WASM build + browser
  tests; publish prebuilt binaries on tags.
- Every code change is complete only when lint, type-check, tests, and build
  all pass.

## 10. Acceptance criteria for milestone 1

1. `h264-aac.mp4` remuxed to Matroska, and `h264-ac3.ts` remuxed to MP4, on
   both Node and Chromium, through a user-supplied async `IoSource` and
   `IoSink`.
2. Reopening each output reports matching stream count, codecs, and duration.
3. `h264-aac-srt.mkv` to MP4 skips the SRT stream by default and fails with
   `onIncompatibleStream: 'fail'`.
4. Non-seekable sink + MP4 without `fragmented` rejects before IO; with
   `fragmented: true` succeeds.
5. Abort mid-remux rejects with `ABORTED` and closes the sink.
6. Measured `.wasm` size recorded in the README.
7. Lint, type-check, tests, and build green in CI on all listed platforms.

## 11. First implementation step

Bootstrap the `aviotrix/aviotrix` repo in the bare-repo + worktrees layout via
the `project-setup` skill, then move this spec into it. No remote push without
Chad's explicit approval.

## 12. Decisions log

| Decision | Choice | Rejected |
|---|---|---|
| First milestone | Thin vertical slice: AVIO + metadata + remux, both targets | Port old native feature set first; WASM-only remux |
| IO model | Async everywhere; core sync, bridges in bindings | Sync-only; dual sync/async paths |
| libav source | Pinned FFmpeg submodule built for both targets | System libav for native |
| Build system | CMake for core + both bindings | node-gyp + Makefile; Zig |
| Packaging | Monorepo, separate scoped packages | Single package with conditional exports |
| Name | aviotrix (`@aviotrix/*`) | avalanche (crypto collision), avanti (`@avanti` squatted) |
| Containers | mp4/mov, matroska/webm, mpegts | Dropping TS to avoid parsers |
| Incompatible streams | Skip with warning by default, `fail` option | Always fail; silent drop |
| Package manager | npm workspaces | pnpm |
| TS toolchain | TypeScript 7 + oxlint + Prettier | TypeScript 5 + typescript-eslint |
