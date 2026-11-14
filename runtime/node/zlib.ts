// xisto:zlib (node:zlib): deflate/inflate, gzip/gunzip, deflateRaw/inflateRaw and unzip, each as *Sync and in the
// callback form, crc32 and constants, over zlib 1.3.1 (third_party/zlib, linked only into programs that import
// this module; runtime/xisto_zlib.h). Options: level, windowBits, memLevel, strategy, dictionary, finishFlush,
// maxOutputLength, chunkSize (the output is produced chunkSize bytes at a time, as Node: level 0's stored blocks
// end where a chunk does, so the bytes are Node's).
// Differences from Node (docs/docs/guides/node-compatibility.md):
//   - input is a string (UTF-8) or a Uint8Array/Buffer (no ArrayBuffer/DataView);
//   - the callback forms compute at once and call back on a later turn of the event loop (Node: the thread pool);
//     on an error the result argument is an empty Buffer (Node: undefined);
//   - no Brotli or Zstd, and no streams (createGzip...) yet.

declare function zlib_run(
  mode: int,
  data: Uint8Array,
  level: int,
  windowBits: int,
  memLevel: int,
  strategy: int,
  dictionary: Uint8Array | null,
  finishFlush: int,
  maxOutputLength: int,
  chunkSize: int
): Buffer | null;
declare function zlib_last_errno(): int;
declare function zlib_last_message(): string;
declare function zlib_crc32(data: Uint8Array, value: number): number;

/** zlib.constants: flush values, return codes, levels, strategies, modes and the option bounds. */
export class ZlibConstants {
  readonly Z_NO_FLUSH: int = 0;
  readonly Z_PARTIAL_FLUSH: int = 1;
  readonly Z_SYNC_FLUSH: int = 2;
  readonly Z_FULL_FLUSH: int = 3;
  readonly Z_FINISH: int = 4;
  readonly Z_BLOCK: int = 5;
  readonly Z_OK: int = 0;
  readonly Z_STREAM_END: int = 1;
  readonly Z_NEED_DICT: int = 2;
  readonly Z_ERRNO: int = -1;
  readonly Z_STREAM_ERROR: int = -2;
  readonly Z_DATA_ERROR: int = -3;
  readonly Z_MEM_ERROR: int = -4;
  readonly Z_BUF_ERROR: int = -5;
  readonly Z_VERSION_ERROR: int = -6;
  readonly Z_NO_COMPRESSION: int = 0;
  readonly Z_BEST_SPEED: int = 1;
  readonly Z_BEST_COMPRESSION: int = 9;
  readonly Z_DEFAULT_COMPRESSION: int = -1;
  readonly Z_FILTERED: int = 1;
  readonly Z_HUFFMAN_ONLY: int = 2;
  readonly Z_RLE: int = 3;
  readonly Z_FIXED: int = 4;
  readonly Z_DEFAULT_STRATEGY: int = 0;
  readonly ZLIB_VERNUM: int = 4880; // (zlib 1.3.1)
  readonly DEFLATE: int = 1;
  readonly INFLATE: int = 2;
  readonly GZIP: int = 3;
  readonly GUNZIP: int = 4;
  readonly DEFLATERAW: int = 5;
  readonly INFLATERAW: int = 6;
  readonly UNZIP: int = 7;
  readonly Z_MIN_WINDOWBITS: int = 8;
  readonly Z_MAX_WINDOWBITS: int = 15;
  readonly Z_DEFAULT_WINDOWBITS: int = 15;
  readonly Z_MIN_CHUNK: int = 64;
  readonly Z_DEFAULT_CHUNK: int = 16384;
  readonly Z_MIN_MEMLEVEL: int = 1;
  readonly Z_MAX_MEMLEVEL: int = 9;
  readonly Z_DEFAULT_MEMLEVEL: int = 8;
  readonly Z_MIN_LEVEL: int = -1;
  readonly Z_MAX_LEVEL: int = 9;
  readonly Z_DEFAULT_LEVEL: int = -1;
}
export const constants: ZlibConstants = new ZlibConstants();

/** The options of every zlib function (Node's ZlibOptions). */
export class ZlibOptions {
  level?: int;
  windowBits?: int;
  memLevel?: int;
  strategy?: int;
  dictionary?: Uint8Array;
  chunkSize?: int;
  flush?: int;
  finishFlush?: int;
  maxOutputLength?: int;
}

/** An error zlib reported: message as Node's ("incorrect header check"...), code (Z_DATA_ERROR...) and errno. */
export class ZlibError extends Error {
  code: string;
  errno: int;
  constructor(message: string, code: string, errno: int) {
    super(message);
    this.code = code;
    this.errno = errno;
  }
}

/** The callback of the asynchronous forms: an error, or the result. */
export type CompressCallback = (error: Error | null, result: Buffer) => void;

function codeName(errno: int): string {
  if (errno === 2) return "Z_NEED_DICT";
  if (errno === 1) return "Z_STREAM_END";
  if (errno === -1) return "Z_ERRNO";
  if (errno === -2) return "Z_STREAM_ERROR";
  if (errno === -3) return "Z_DATA_ERROR";
  if (errno === -4) return "Z_MEM_ERROR";
  if (errno === -5) return "Z_BUF_ERROR";
  if (errno === -6) return "Z_VERSION_ERROR";
  return "Z_UNKNOWN";
}

// a RangeError with Node's code (ERR_OUT_OF_RANGE...): an Error named RangeError (xisto cannot extend RangeError)
class RangeCodeError extends Error {
  code: string;
  constructor(message: string, code: string) {
    super(message);
    this.name = "RangeError";
    this.code = code;
  }
}

function checkRange(name: string, v: int, lo: int, hi: int): int {
  if (v < lo || v > hi) {
    throw new RangeCodeError(`The value of "options.${name}" is out of range. It must be >= ${lo} and <= ${hi}. Received ${v}`, "ERR_OUT_OF_RANGE");
  }
  return v;
}

function bytesOf(data: string | Uint8Array): Uint8Array {
  if (typeof data === "string") return Buffer.from(data as string, "utf8");
  return data as Uint8Array;
}

function run(mode: int, data: string | Uint8Array, opts: ZlibOptions | null): Buffer {
  const o: ZlibOptions = opts !== null ? opts : new ZlibOptions();
  const level = checkRange("level", o.level ?? -1, -1, 9);
  const windowBits = checkRange("windowBits", o.windowBits ?? 15, 8, 15);
  const memLevel = checkRange("memLevel", o.memLevel ?? 8, 1, 9);
  const strategy = checkRange("strategy", o.strategy ?? 0, 0, 4);
  const finishFlush = checkRange("finishFlush", o.finishFlush ?? 4, 0, 5);
  const maxOut = o.maxOutputLength ?? -1;
  const dict = o.dictionary ?? null;
  const chunk = o.chunkSize ?? 16384;
  if (chunk < 64) throw new RangeCodeError(`The value of "options.chunkSize" is out of range. It must be >= 64. Received ${chunk}`, "ERR_OUT_OF_RANGE");
  const out = zlib_run(mode, bytesOf(data), level, windowBits, memLevel, strategy, dict, finishFlush, maxOut, chunk);
  if (out !== null) return out;
  const errno = zlib_last_errno();
  if (errno === 1) throw new RangeCodeError(`Cannot create a Buffer larger than ${maxOut} bytes`, "ERR_BUFFER_TOO_LARGE");
  throw new ZlibError(zlib_last_message(), codeName(errno), errno);
}

/** Compresses with deflate (a zlib stream: header, data, Adler-32). */
export function deflateSync(data: string | Uint8Array, options: ZlibOptions | null = null): Buffer {
  return run(1, data, options);
}
/** Decompresses a zlib stream. */
export function inflateSync(data: string | Uint8Array, options: ZlibOptions | null = null): Buffer {
  return run(2, data, options);
}
/** Compresses into a gzip member (header, data, CRC-32 and length). */
export function gzipSync(data: string | Uint8Array, options: ZlibOptions | null = null): Buffer {
  return run(3, data, options);
}
/** Decompresses gzip data (members one after the other are all read, as Node's). */
export function gunzipSync(data: string | Uint8Array, options: ZlibOptions | null = null): Buffer {
  return run(4, data, options);
}
/** Compresses with raw deflate (no header, no checksum). */
export function deflateRawSync(data: string | Uint8Array, options: ZlibOptions | null = null): Buffer {
  return run(5, data, options);
}
/** Decompresses raw deflate. */
export function inflateRawSync(data: string | Uint8Array, options: ZlibOptions | null = null): Buffer {
  return run(6, data, options);
}
/** Decompresses gzip or zlib data, whichever its header says. */
export function unzipSync(data: string | Uint8Array, options: ZlibOptions | null = null): Buffer {
  return run(7, data, options);
}

// the callback forms: computed now, the callback called on a later turn of the event loop
function later(mode: int, data: string | Uint8Array, opts: ZlibOptions | null, cb: CompressCallback): void {
  let err: Error | null = null;
  let out: Buffer = Buffer.alloc(0);
  try {
    out = run(mode, data, opts);
  } catch (e) {
    err = e as Error;
  }
  setTimeout(() => {
    cb(err, out);
  }, 0);
}

/** deflateSync, its result given to callback. */
export function deflate(data: string | Uint8Array, options: ZlibOptions | CompressCallback, callback: CompressCallback | null = null): void {
  dispatch(1, data, options, callback);
}
/** inflateSync, its result given to callback. */
export function inflate(data: string | Uint8Array, options: ZlibOptions | CompressCallback, callback: CompressCallback | null = null): void {
  dispatch(2, data, options, callback);
}
/** gzipSync, its result given to callback. */
export function gzip(data: string | Uint8Array, options: ZlibOptions | CompressCallback, callback: CompressCallback | null = null): void {
  dispatch(3, data, options, callback);
}
/** gunzipSync, its result given to callback. */
export function gunzip(data: string | Uint8Array, options: ZlibOptions | CompressCallback, callback: CompressCallback | null = null): void {
  dispatch(4, data, options, callback);
}
/** deflateRawSync, its result given to callback. */
export function deflateRaw(data: string | Uint8Array, options: ZlibOptions | CompressCallback, callback: CompressCallback | null = null): void {
  dispatch(5, data, options, callback);
}
/** inflateRawSync, its result given to callback. */
export function inflateRaw(data: string | Uint8Array, options: ZlibOptions | CompressCallback, callback: CompressCallback | null = null): void {
  dispatch(6, data, options, callback);
}
/** unzipSync, its result given to callback. */
export function unzip(data: string | Uint8Array, options: ZlibOptions | CompressCallback, callback: CompressCallback | null = null): void {
  dispatch(7, data, options, callback);
}

function dispatch(mode: int, data: string | Uint8Array, options: ZlibOptions | CompressCallback, callback: CompressCallback | null): void {
  if (callback !== null) {
    later(mode, data, options as ZlibOptions, callback);
  } else {
    later(mode, data, null, options as CompressCallback);
  }
}

/** CRC-32 of data (UTF-8 for a string), continued from value (the CRC of what came before; 0 to start). */
export function crc32(data: string | Uint8Array, value: number = 0): number {
  return zlib_crc32(bytesOf(data), value);
}
