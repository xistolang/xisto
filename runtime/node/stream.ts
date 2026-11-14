// xisto:stream (node:stream): Readable, Writable, Duplex, Transform and PassThrough on xisto:events' EventEmitter,
// with Node's state machines (paused and flowing modes, highWaterMark and backpressure, 'drain', cork/uncork, _writev,
// _final, _construct, destroy), pipe/unpipe, pipeline, finished, Readable.from and for await over a Readable; and
// fs.createReadStream / fs.createWriteStream (ReadStream, WriteStream).
// It is a port of Node's lib/internal/streams (v26): the same flags, the same process.nextTick deferrals, so events come
// in the same order and on the same ticks as in Node. Differences (docs/docs/guides/node-compatibility.md):
//   - Readable is a subclass of Writable here (a Duplex has to be both: xisto classes have one base), so a Readable
//     has write()/end() (they throw) and `instanceof Writable` is true for it; `instanceof Readable` is exact;
//   - option functions (read, write, transform...) get the stream as `this` only when written as
//     function (this: Readable, ...) { }; an arrow function has none;
//   - Readable.from consumes a generator as a task (a sync generator runs until push() asks it to wait), not with
//     iterator.next() calls; for await over a Readable reads it with 'readable'/read() as Node does;
//   - errors are Error objects (destroy("text") is not accepted), and a missing argument is null, not undefined.

import { EventEmitter } from "xisto:events";
import type { Listener } from "xisto:events";
import { TypeError, RangeError } from "xisto:errors";
import { closeSync, fstatSync, openSync, readSync } from "xisto:fs";

declare function fs_stream_open(path: string, flags: string, mode: int): int;
declare function fs_stream_write(fd: int, b: Uint8Array, off: int, len: int, pos: int): int;
declare function fs_stream_errno(): int;
declare function fs_stream_strerror(): string;

/** A callback of a write, an end, a destroy: an error, or nothing. */
export type ErrorCallback = (error?: Error | null) => void;
/** What _transform and _flush call when they are done: an error, or nothing and optionally a chunk to push. */
export type TransformCallback = (error?: Error | null, data?: any) => void;

// ---- errors (Node's codes and messages) ----

class StreamError extends Error {
  code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}
class StreamTypeError extends TypeError {
  code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}
class StreamRangeError extends RangeError {
  code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}
/** Node's AbortError (name "AbortError", code "ABORT_ERR"). */
export class AbortError extends Error {
  code: string = "ABORT_ERR";
  constructor(message: string = "The operation was aborted") {
    super(message);
    this.name = "AbortError";
  }
}
function errDestroyed(what: string): Error {
  return new StreamError("ERR_STREAM_DESTROYED", `Cannot call ${what} after a stream was destroyed`);
}
function errPrematureClose(): Error {
  return new StreamError("ERR_STREAM_PREMATURE_CLOSE", "Premature close");
}
function errMultipleCallback(): Error {
  return new StreamError("ERR_MULTIPLE_CALLBACK", "Callback called multiple times");
}
function errNotImplemented(m: string): Error {
  return new StreamError("ERR_METHOD_NOT_IMPLEMENTED", `The ${m} method is not implemented`);
}
function describeReceived(v: any): string {
  if (v === null) return "null";
  if (typeof v === "number" || typeof v === "boolean") return `type ${typeof v} (${String(v)})`;
  if (typeof v === "string") return `type string ('${v as string}')`;
  return "an instance of Object";
}
function errInvalidChunk(v: any): Error {
  return new StreamTypeError(
    "ERR_INVALID_ARG_TYPE",
    `The "chunk" argument must be of type string or an instance of Buffer, TypedArray, or DataView. Received ${describeReceived(v)}`,
  );
}
function errorCode(e: Error | null): string {
  if (e === null) return "";
  if (e instanceof StreamError) return e.code;
  if (e instanceof StreamTypeError) return e.code;
  if (e instanceof AbortError) return e.code;
  return "";
}
function aggregateTwoErrors(inner: Error | null, outer: Error | null): Error | null {
  if (inner !== null && outer !== null && inner !== outer) return outer; // (Node: an AggregateError of both)
  return inner !== null ? inner : outer;
}

const nop: ErrorCallback = (e?: Error | null): void => {};

// ---- high water marks ----

let defaultHwmBytes: int = 65536;
let defaultHwmObjects: int = 16;
/** The highWaterMark a stream gets when none is given (64 KiB of bytes, or 16 objects). */
export function getDefaultHighWaterMark(objectMode: boolean): int {
  return objectMode ? defaultHwmObjects : defaultHwmBytes;
}
/** Changes the default highWaterMark of the streams made from now on. */
export function setDefaultHighWaterMark(objectMode: boolean, value: int): void {
  if (value < 0) throw new StreamRangeError("ERR_OUT_OF_RANGE", `The value of "value" is out of range. It must be >= 0. Received ${value}`);
  if (objectMode) defaultHwmObjects = value;
  else defaultHwmBytes = value;
}

// ---- the decoder of setEncoding() / the encoding option (Node's StringDecoder) ----

function normEncoding(enc: string): string {
  const e = enc.toLowerCase();
  if (e === "utf8" || e === "utf-8") return "utf8";
  if (e === "ucs2" || e === "ucs-2" || e === "utf16le" || e === "utf-16le") return "utf16le";
  if (e === "binary" || e === "latin1") return "latin1";
  if (e === "base64" || e === "base64url" || e === "hex" || e === "ascii") return e;
  throw new StreamTypeError("ERR_UNKNOWN_ENCODING", `Unknown encoding: ${enc}`);
}

/** Bytes to text, keeping a character cut between two chunks until the rest of it comes. */
class Decoder {
  encoding: string;
  private rest: Uint8Array = new Uint8Array(0);
  constructor(enc: string) {
    this.encoding = normEncoding(enc);
  }
  /** How many bytes at the end of b do not make a whole unit yet. */
  private incomplete(b: Uint8Array): int {
    const n = b.length;
    if (this.encoding === "utf8") {
      for (let back = 1; back <= 3 && back <= n; back++) {
        const c = b[n - back];
        if ((c & 0xc0) === 0x80) continue; // a continuation byte
        const need = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        return need > back ? back : 0;
      }
      return 0;
    }
    if (this.encoding === "utf16le") {
      if (n % 2 === 1) return 1;
      if (n >= 2) {
        const hi = b[n - 1];
        if (hi >= 0xd8 && hi <= 0xdb) return 2; // a lead surrogate: wait for its pair
      }
      return 0;
    }
    if (this.encoding === "base64" || this.encoding === "base64url") return n % 3;
    return 0;
  }
  write(chunk: Uint8Array): string {
    let b = chunk;
    if (this.rest.length > 0) {
      b = Buffer.concat([asBuffer(this.rest), asBuffer(chunk)]);
      this.rest = new Uint8Array(0);
    }
    const keep = this.incomplete(b);
    if (keep > 0) {
      this.rest = b.slice(b.length - keep);
      b = b.subarray(0, b.length - keep);
    }
    if (b.length === 0) return "";
    return Buffer.from(b.buffer, b.byteOffset, b.length).toString(this.encoding);
  }
  end(): string {
    if (this.rest.length === 0) return "";
    const r = this.rest;
    this.rest = new Uint8Array(0);
    return Buffer.from(r.buffer, r.byteOffset, r.length).toString(this.encoding);
  }
}

function chunkLength(c: any): int {
  if (typeof c === "string") return (c as string).length;
  return (c as Uint8Array).length;
}
function isBytes(c: any): boolean {
  return c instanceof Uint8Array;
}
function asBuffer(c: any): Buffer {
  const u = c as Uint8Array;
  return Buffer.from(u.buffer, u.byteOffset, u.length);
}
function toBuffer(s: string, enc: string): Buffer {
  return Buffer.from(s, enc);
}

// ---- options ----

// _writev()'s chunks are written as an object type in place, { chunk: any; encoding: string }[], so that the same
// type written in a subclass or a writev option (Node's typings: { chunk: any; encoding: BufferEncoding }[]) is it

/** The options of the stream constructors (Readable, Writable, Duplex, Transform, PassThrough). */
export interface StreamOptions {
  highWaterMark?: int;
  readableHighWaterMark?: int;
  writableHighWaterMark?: int;
  objectMode?: boolean;
  readableObjectMode?: boolean;
  writableObjectMode?: boolean;
  encoding?: string;
  defaultEncoding?: string;
  decodeStrings?: boolean;
  emitClose?: boolean;
  autoDestroy?: boolean;
  allowHalfOpen?: boolean;
  readable?: boolean;
  writable?: boolean;
  read?: (size: number) => void;
  write?: (chunk: any, encoding: string, callback: ErrorCallback) => void;
  writev?: (chunks: { chunk: any; encoding: string }[], callback: ErrorCallback) => void;
  final?: (callback: ErrorCallback) => void;
  destroy?: (error: Error | null, callback: ErrorCallback) => void;
  construct?: (callback: ErrorCallback) => void;
  transform?: (chunk: any, encoding: string, callback: TransformCallback) => void;
  flush?: (callback: TransformCallback) => void;
}
export type ReadableOptions = StreamOptions;
export type WritableOptions = StreamOptions;
export type DuplexOptions = StreamOptions;
export type TransformOptions = StreamOptions;

function hwmOf(opts: StreamOptions | null, duplexKey: int, isDuplex: boolean, objectMode: boolean): int {
  if (opts !== null) {
    const h = opts.highWaterMark;
    if (h !== null && h !== undefined) {
      if (h < 0) throw new StreamTypeError("ERR_INVALID_ARG_VALUE", `The property 'options.highWaterMark' is invalid. Received ${h}`);
      return h;
    }
    if (isDuplex) {
      const d = duplexKey === 0 ? opts.readableHighWaterMark : opts.writableHighWaterMark;
      if (d !== null && d !== undefined) return d;
    }
  }
  return getDefaultHighWaterMark(objectMode);
}

// ---- states ----

/** The state of a stream's readable side (stream._readableState). */
export class ReadableState {
  objectMode: boolean = false;
  highWaterMark: int = 0;
  buffer: any[] = [];
  bufferIndex: int = 0;
  length: int = 0;
  pipes: Writable[] = [];
  ended: boolean = false;
  endEmitted: boolean = false;
  reading: boolean = false;
  constructed: boolean = true;
  sync: boolean = true;
  needReadable: boolean = false;
  emittedReadable: boolean = false;
  readableListening: boolean = false;
  resumeScheduled: boolean = false;
  errorEmitted: boolean = false;
  emitClose: boolean = true;
  autoDestroy: boolean = true;
  destroyed: boolean = false;
  closed: boolean = false;
  closeEmitted: boolean = false;
  multiAwaitDrain: boolean = false;
  readingMore: boolean = false;
  dataEmitted: boolean = false;
  hasFlowing: boolean = false;
  flowingOn: boolean = false;
  hasPaused: boolean = false;
  paused: boolean = false;
  dataListening: boolean = false;
  endScheduled: boolean = false;
  eofReadablePending: boolean = false;
  readableOff: boolean = false; // readable: false (Duplex option)
  errored: Error | null = null;
  defaultEncoding: string = "utf8";
  decoder: Decoder | null = null;
  encoding: string | null = null;
  awaitDrainWriter: Writable | null = null;
  awaitDrainSet: Writable[] = [];

  /** true (flowing), false (paused), or null (neither yet), as readable.readableFlowing. */
  get flowing(): boolean | null {
    return this.hasFlowing ? this.flowingOn : null;
  }
  get pipesCount(): int {
    return this.pipes.length;
  }
}

class WriteReq {
  chunk: any;
  encoding: string;
  callback: ErrorCallback;
  constructor(chunk: any, encoding: string, callback: ErrorCallback) {
    this.chunk = chunk;
    this.encoding = encoding;
    this.callback = callback;
  }
}

class AfterWriteTickInfo {
  count: int = 1;
  cb: ErrorCallback;
  constructor(cb: ErrorCallback) {
    this.cb = cb;
  }
}

/** The state of a stream's writable side (stream._writableState). */
export class WritableState {
  objectMode: boolean = false;
  highWaterMark: int = 0;
  finalCalled: boolean = false;
  needDrain: boolean = false;
  ending: boolean = false;
  ended: boolean = false;
  finished: boolean = false;
  destroyed: boolean = false;
  decodeStrings: boolean = true;
  writing: boolean = false;
  sync: boolean = true;
  bufferProcessing: boolean = false;
  constructed: boolean = true;
  prefinished: boolean = false;
  errorEmitted: boolean = false;
  emitClose: boolean = true;
  autoDestroy: boolean = true;
  closed: boolean = false;
  closeEmitted: boolean = false;
  allBuffers: boolean = true;
  allNoop: boolean = true;
  writableOff: boolean = false; // writable: false (Duplex option)
  errored: Error | null = null;
  defaultEncoding: string = "utf8";
  length: int = 0;
  corked: int = 0;
  writelen: int = 0;
  buffered: WriteReq[] = [];
  bufferedIndex: int = 0;
  pendingcb: int = 0;
  writecb: ErrorCallback | null = null;
  expectWriteCb: boolean = false;
  afterWritePending: boolean = false;
  afterWriteTickInfo: AfterWriteTickInfo | null = null;
  onFinished: ErrorCallback[] = [];

  /** The chunks waiting to be written. */
  getBuffer(): { chunk: any; encoding: string }[] {
    const out: { chunk: any; encoding: string }[] = [];
    for (let i = this.bufferedIndex; i < this.buffered.length; i++) {
      const b = this.buffered[i];
      out.push({ chunk: b.chunk, encoding: b.encoding });
    }
    return out;
  }
  get bufferedRequestCount(): int {
    return this.buffered.length - this.bufferedIndex;
  }
}

function resetBuffer(state: WritableState): void {
  state.buffered = [];
  state.bufferedIndex = 0;
  state.allBuffers = true;
  state.allNoop = true;
}

// ---- Stream: what both sides share (destroy, the state getters) ----

/** The base of every stream: an EventEmitter with a readable side, a writable side, or both. */
export class Stream extends EventEmitter {
  _readableState: ReadableState | null = null;
  _writableState: WritableState | null = null;
  /** A Duplex that keeps its writable side open after the readable side ends (Node's default: true). */
  allowHalfOpen: boolean = true;
  protected _optRead: ((size: number) => void) | null = null;
  protected _optWrite: ((chunk: any, encoding: string, callback: ErrorCallback) => void) | null = null;
  protected _optWritev: ((chunks: { chunk: any; encoding: string }[], callback: ErrorCallback) => void) | null = null;
  protected _optFinal: ((callback: ErrorCallback) => void) | null = null;
  protected _optDestroy: ((error: Error | null, callback: ErrorCallback) => void) | null = null;
  protected _optConstruct: ((callback: ErrorCallback) => void) | null = null;
  private _constructCbs: (() => void)[] = [];
  private _destroyWaiters: ((err: Error | null) => void)[] = [];

  constructor() {
    super();
  }

  protected _takeOptions(opts: StreamOptions | null): void {
    if (opts === null) return;
    const r = opts.read;
    if (r !== null && r !== undefined) this._optRead = r;
    const w = opts.write;
    if (w !== null && w !== undefined) this._optWrite = w;
    const wv = opts.writev;
    if (wv !== null && wv !== undefined) this._optWritev = wv;
    const f = opts.final;
    if (f !== null && f !== undefined) this._optFinal = f;
    const d = opts.destroy;
    if (d !== null && d !== undefined) this._optDestroy = d;
    const c = opts.construct;
    if (c !== null && c !== undefined) this._optConstruct = c;
  }

  /** Whether the stream has its own _construct (a subclass's or the construct option). */
  protected _hasConstruct(): boolean {
    return this._optConstruct !== null || __xs_overrides(this as Stream, "_construct");
  }
  /** Whether the stream has its own _final (a subclass's or the final option). */
  _hasFinal(): boolean {
    return this._optFinal !== null || __xs_overrides(this as Stream, "_final");
  }
  /** Whether the stream has its own _writev (a subclass's or the writev option). */
  _hasWritev(): boolean {
    return this._optWritev !== null || __xs_overrides(this as Stream, "_writev");
  }

  // the subclass hooks: Node's _read, _write, _writev, _final, _destroy, _construct
  _read(size: number): void {
    const f = this._optRead;
    if (f === null) throw errNotImplemented("_read()");
    __xs_this_set(this);
    f(size);
    __xs_this_set(null);
  }
  _write(chunk: any, encoding: string, callback: ErrorCallback): void {
    const f = this._optWrite;
    if (f !== null) {
      __xs_this_set(this);
      f(chunk, encoding, callback);
      __xs_this_set(null);
      return;
    }
    if (this._hasWritev()) {
      this._writev([{ chunk: chunk, encoding: encoding }], callback);
      return;
    }
    throw errNotImplemented("_write()");
  }
  _writev(chunks: { chunk: any; encoding: string }[], callback: ErrorCallback): void {
    const f = this._optWritev;
    if (f === null) throw errNotImplemented("_writev()");
    __xs_this_set(this);
    f(chunks, callback);
    __xs_this_set(null);
  }
  _final(callback: ErrorCallback): void {
    const f = this._optFinal;
    if (f === null) {
      callback(null);
      return;
    }
    __xs_this_set(this);
    f(callback);
    __xs_this_set(null);
  }
  _destroy(error: Error | null, callback: ErrorCallback): void {
    const f = this._optDestroy;
    if (f === null) {
      callback(error);
      return;
    }
    __xs_this_set(this);
    f(error, callback);
    __xs_this_set(null);
  }
  _construct(callback: ErrorCallback): void {
    const f = this._optConstruct;
    if (f === null) {
      callback(null);
      return;
    }
    __xs_this_set(this);
    f(callback);
    __xs_this_set(null);
  }

  /** Node's destroyImpl.construct: _construct on the next tick; reads and writes wait for it. */
  protected _startConstruct(then: () => void): void {
    const r = this._readableState;
    const w = this._writableState;
    if (r !== null) r.constructed = false;
    if (w !== null) w.constructed = false;
    this._constructCbs.push(then);
    if (this._constructCbs.length > 1) return;
    process.nextTick((): void => this._constructNT());
  }
  private _constructNT(): void {
    let called = false;
    const onConstruct = (err: Error | null): void => {
      if (called) {
        errorOrDestroy(this, err !== null ? err : errMultipleCallback(), false);
        return;
      }
      called = true;
      const r = this._readableState;
      const w = this._writableState;
      if (r !== null) r.constructed = true;
      if (w !== null) w.constructed = true;
      if (this._isDestroyedState()) {
        const ws = this._destroyWaiters;
        this._destroyWaiters = [];
        for (const f of ws) f(err);
      } else if (err !== null) {
        errorOrDestroy(this, err, true);
      } else {
        const cbs = this._constructCbs;
        this._constructCbs = [];
        for (const f of cbs) f();
      }
    };
    try {
      this._construct((err?: Error | null): void => {
        const e: Error | null = err !== undefined ? err : null;
        process.nextTick((): void => onConstruct(e));
      });
    } catch (e) {
      const ee = e as Error;
      process.nextTick((): void => onConstruct(ee));
    }
  }
  private _isDestroyedState(): boolean {
    const w = this._writableState;
    if (w !== null) return w.destroyed;
    const r = this._readableState;
    return r !== null && r.destroyed;
  }
  private _isConstructed(): boolean {
    const w = this._writableState;
    if (w !== null) return w.constructed;
    const r = this._readableState;
    return r === null || r.constructed;
  }

  /** Destroys the stream: _destroy, then 'error' (when err is given) and 'close' on later ticks. */
  destroy(err: Error | null = null, cb: ErrorCallback | null = null): Stream {
    const r = this._readableState;
    const w = this._writableState;
    if (w !== null && (w.bufferedRequestCount > 0 || w.onFinished.length > 0) && !w.destroyed) {
      process.nextTick((): void => errorBuffer(w));
    }
    if ((w !== null && w.destroyed) || (r !== null && r.destroyed)) {
      if (cb !== null) cb(null);
      return this;
    }
    checkError(err, w, r);
    if (w !== null) w.destroyed = true;
    if (r !== null) r.destroyed = true;
    if (!this._isConstructed()) {
      this._destroyWaiters.push((er: Error | null): void => {
        this._destroyNow(aggregateTwoErrors(er, err), cb);
      });
    } else {
      this._destroyNow(err, cb);
    }
    return this;
  }
  private _destroyNow(err: Error | null, cb: ErrorCallback | null): void {
    let called = false;
    const onDestroy = (e: Error | null): void => {
      if (called) return;
      called = true;
      const r = this._readableState;
      const w = this._writableState;
      checkError(e, w, r);
      if (w !== null) w.closed = true;
      if (r !== null) r.closed = true;
      if (cb !== null) cb(e);
      if (e !== null) {
        process.nextTick((): void => {
          emitErrorNT(this, e);
          emitCloseNT(this);
        });
      } else {
        process.nextTick((): void => emitCloseNT(this));
      }
    };
    try {
      this._destroy(err, (e?: Error | null): void => onDestroy(e !== undefined ? e : null));
    } catch (e) {
      onDestroy(e as Error);
    }
  }
  /** Brings a destroyed stream back to its first state. */
  _undestroy(): void {
    const r = this._readableState;
    const w = this._writableState;
    if (r !== null) {
      r.constructed = true;
      r.closed = false;
      r.closeEmitted = false;
      r.destroyed = false;
      r.errored = null;
      r.errorEmitted = false;
      r.reading = false;
      r.ended = r.readableOff;
      r.endEmitted = r.readableOff;
    }
    if (w !== null) {
      w.constructed = true;
      w.destroyed = false;
      w.closed = false;
      w.closeEmitted = false;
      w.errored = null;
      w.errorEmitted = false;
      w.finalCalled = false;
      w.prefinished = false;
      w.ended = w.writableOff;
      w.ending = w.writableOff;
      w.finished = w.writableOff;
    }
  }

  /** true once destroy() was called (on both sides, for a Duplex). */
  get destroyed(): boolean {
    const r = this._readableState;
    const w = this._writableState;
    if (r !== null && w !== null) return r.destroyed && w.destroyed;
    if (w !== null) return w.destroyed;
    if (r !== null) return r.destroyed;
    return false;
  }
  set destroyed(v: boolean) {
    const r = this._readableState;
    const w = this._writableState;
    if (r !== null) r.destroyed = v;
    if (w !== null) w.destroyed = v;
  }
  /** true once 'close' is due (the stream is done destroying). */
  get closed(): boolean {
    const w = this._writableState;
    if (w !== null) return w.closed;
    const r = this._readableState;
    return r !== null && r.closed;
  }
  /** The error the stream was destroyed with, or null. */
  get errored(): Error | null {
    const w = this._writableState;
    if (w !== null && this._readableState === null) return w.errored;
    const r = this._readableState;
    return r !== null ? r.errored : null;
  }
}

function checkError(err: Error | null, w: WritableState | null, r: ReadableState | null): void {
  if (err === null) return;
  if (w !== null && w.errored === null) w.errored = err;
  if (r !== null && r.errored === null) r.errored = err;
}
function emitCloseNT(self: Stream): void {
  const r = self._readableState;
  const w = self._writableState;
  if (w !== null) w.closeEmitted = true;
  if (r !== null) r.closeEmitted = true;
  if ((w !== null && w.emitClose) || (r !== null && r.emitClose)) self.emit("close");
}
function emitErrorNT(self: Stream, err: Error): void {
  const r = self._readableState;
  const w = self._writableState;
  if ((w !== null && w.errorEmitted) || (r !== null && r.errorEmitted)) return;
  if (w !== null) w.errorEmitted = true;
  if (r !== null) r.errorEmitted = true;
  self.emit("error", err);
}
/** Node's errorOrDestroy: destroy(err) when the stream destroys itself on errors (autoDestroy), else 'error'. */
function errorOrDestroy(stream: Stream, err: Error | null, sync: boolean): void {
  const r = stream._readableState;
  const w = stream._writableState;
  if ((w !== null && w.destroyed) || (r !== null && r.destroyed)) return;
  if ((r !== null && r.autoDestroy) || (w !== null && w.autoDestroy)) {
    stream.destroy(err);
  } else if (err !== null) {
    if (w !== null && w.errored === null) w.errored = err;
    if (r !== null && r.errored === null) r.errored = err;
    if (sync) {
      const e = err;
      process.nextTick((): void => emitErrorNT(stream, e));
    } else {
      emitErrorNT(stream, err);
    }
  }
}

// ---- Writable ----

/** A destination of chunks: write() each one (false: wait for 'drain'), end() once; 'finish' when all are written. */
export class Writable extends Stream {
  constructor(options: StreamOptions | null = null) {
    super();
    if (this instanceof Readable) return; // (Readable and Duplex make their own states)
    this._writableState = newWritableState(options, false);
    this._takeOptions(options);
    if (this._hasConstruct()) this._startConstruct((): void => writableOnConstructed(this));
  }

  /** Writes a chunk (a string, a Buffer or Uint8Array, any value in object mode); false: the buffer is above the
   * highWaterMark, wait for 'drain'. write(chunk, cb) and write(chunk, encoding, cb) as in Node. */
  write(chunk: any, encoding: string | ErrorCallback | null = null, cb: ErrorCallback | null = null): boolean {
    let enc: string | null = null;
    let f: ErrorCallback | null = cb;
    if (typeof encoding === "string") enc = encoding;
    else if (encoding !== null) f = encoding;
    const r = writeImpl(this, chunk, enc, f !== null ? f : nop);
    return r === 1;
  }

  /** Holds the writes in the buffer until uncork() (or end()). */
  cork(): void {
    const state = mustWritable(this);
    state.corked++;
  }
  /** Lets the writes held by cork() go (once every cork() has its uncork()). */
  uncork(): void {
    const state = mustWritable(this);
    if (state.corked > 0) {
      state.corked--;
      if (!state.writing) clearBuffer(this, state);
    }
  }
  /** The encoding of string chunks written without one. */
  setDefaultEncoding(encoding: string): Writable {
    const state = mustWritable(this);
    state.defaultEncoding = normEncoding(encoding) === "utf8" ? "utf8" : encoding.toLowerCase();
    return this;
  }

  /** No more writes (after an optional last chunk): 'finish' once everything is written; cb gets the end's error, or
   * null. end(cb), end(chunk, cb), end(chunk, encoding, cb) as in Node. */
  end(chunk: any = null, encoding: string | ErrorCallback | null = null, cb: ErrorCallback | null = null): Writable {
    const state = mustWritable(this);
    let c: any = chunk;
    let enc: string | null = null;
    let f: ErrorCallback | null = cb;
    if (typeof encoding === "string") enc = encoding;
    else if (encoding !== null) f = encoding;
    if (c !== null && typeof c === "function") { // (end(cb): the callback given as the chunk)
      f = c as ErrorCallback;
      c = null;
    }
    let err: Error | null = null;
    if (c !== null) {
      const ret = writeImpl(this, c, enc, nop);
      if (ret === 2) err = lastWriteError;
    }
    if (state.corked > 0) {
      state.corked = 1;
      this.uncork();
    }
    if (err !== null) {
      // (the write's error is reported by it)
    } else if (!state.ending && state.errored === null) {
      state.ending = true;
      finishMaybe(this, state, true);
      state.ended = true;
    } else if (state.finished) {
      err = new StreamError("ERR_STREAM_ALREADY_FINISHED", "Cannot call end after a stream was finished");
    } else if (state.destroyed) {
      err = errDestroyed("end");
    }
    if (f !== null) {
      const fn = f;
      if (err !== null) {
        const e = err;
        process.nextTick((): void => fn(e));
      } else if (state.errored !== null) {
        const e = state.errored;
        process.nextTick((): void => fn(e));
      } else if (state.finished) {
        process.nextTick((): void => fn(null));
      } else {
        state.onFinished.push(fn);
      }
    }
    return this;
  }

  /** Whether write() may be called: not ended, destroyed or errored. */
  get writable(): boolean {
    const w = this._writableState;
    return w !== null && !w.writableOff && !w.ending && !w.ended && !w.destroyed && w.errored === null;
  }
  set writable(v: boolean) {
    const w = this._writableState;
    if (w !== null) w.writableOff = !v;
  }
  get writableEnded(): boolean {
    const w = this._writableState;
    return w !== null && w.ending;
  }
  get writableFinished(): boolean {
    const w = this._writableState;
    return w !== null && w.finished;
  }
  get writableLength(): int {
    const w = this._writableState;
    return w !== null ? w.length : 0;
  }
  get writableHighWaterMark(): int {
    const w = this._writableState;
    return w !== null ? w.highWaterMark : 0;
  }
  get writableObjectMode(): boolean {
    const w = this._writableState;
    return w !== null && w.objectMode;
  }
  get writableCorked(): int {
    const w = this._writableState;
    return w !== null ? w.corked : 0;
  }
  get writableNeedDrain(): boolean {
    const w = this._writableState;
    return w !== null && !w.destroyed && !w.ending && w.needDrain;
  }
  get writableBuffer(): { chunk: any; encoding: string }[] {
    const w = this._writableState;
    return w !== null ? w.getBuffer() : [];
  }
  get writableAborted(): boolean {
    const w = this._writableState;
    return w !== null && !w.writableOff && (w.destroyed || w.errored !== null) && !w.finished;
  }
}

function mustWritable(s: Stream): WritableState {
  const w = s._writableState;
  if (w === null) throw new StreamTypeError("ERR_INVALID_STATE", "the stream is not writable");
  return w;
}

function newWritableState(opts: StreamOptions | null, isDuplex: boolean): WritableState {
  const s = new WritableState();
  if (opts !== null) {
    if (opts.objectMode === true) s.objectMode = true;
    if (isDuplex && opts.writableObjectMode === true) s.objectMode = true;
    if (opts.decodeStrings === false) s.decodeStrings = false;
    if (opts.emitClose === false) s.emitClose = false;
    if (opts.autoDestroy === false) s.autoDestroy = false;
    const de = opts.defaultEncoding;
    if (de !== null && de !== undefined) s.defaultEncoding = normEncoding(de) === "utf8" ? "utf8" : de;
  }
  s.highWaterMark = hwmOf(opts, 1, isDuplex, s.objectMode);
  return s;
}

function writableOnConstructed(stream: Stream): void {
  const state = stream._writableState;
  if (state === null) return;
  if (!state.writing) clearBuffer(stream, state);
  if (state.ending) finishMaybe(stream, state, false);
}

let lastWriteError: Error | null = null;
/** Node's _write(): 1 written or buffered below the mark, 0 above it, 2 an error (lastWriteError). */
function writeImpl(stream: Stream, chunk0: any, encoding: string | null, cb: ErrorCallback): int {
  const state = mustWritable(stream);
  let chunk: any = chunk0;
  let enc = encoding;
  if (chunk === null) throw new StreamTypeError("ERR_STREAM_NULL_VALUES", "May not write null values to stream");
  if (!state.objectMode) {
    let e2: string = enc !== null && enc !== "" ? enc : state.defaultEncoding;
    if (e2 !== "buffer") normEncoding(e2);
    if (typeof chunk === "string") {
      if (e2 === "buffer") throw new StreamTypeError("ERR_UNKNOWN_ENCODING", "Unknown encoding: buffer");
      if (state.decodeStrings) {
        chunk = toBuffer(chunk as string, e2);
        e2 = "buffer";
      }
    } else if (isBytes(chunk)) {
      e2 = "buffer";
    } else {
      throw errInvalidChunk(chunk);
    }
    enc = e2;
  }
  let err: Error | null = null;
  if (state.ending) err = new StreamError("ERR_STREAM_WRITE_AFTER_END", "write after end");
  else if (state.destroyed) err = errDestroyed("write");
  if (err !== null) {
    const e = err;
    process.nextTick((): void => cb(e));
    errorOrDestroy(stream, e, true);
    lastWriteError = e;
    return 2;
  }
  state.pendingcb++;
  return writeOrBuffer(stream, state, chunk, enc !== null ? enc : "", cb) ? 1 : 0;
}

function writeOrBuffer(stream: Stream, state: WritableState, chunk: any, encoding: string, callback: ErrorCallback): boolean {
  const len = state.objectMode ? 1 : chunkLength(chunk);
  state.length += len;
  if (state.writing || state.errored !== null || state.corked > 0 || !state.constructed) {
    state.buffered.push(new WriteReq(chunk, encoding, callback));
    if (state.allBuffers && encoding !== "buffer") state.allBuffers = false;
    if (state.allNoop && callback !== nop) state.allNoop = false;
  } else {
    state.writelen = len;
    if (callback !== nop) state.writecb = callback;
    state.writing = true;
    state.sync = true;
    state.expectWriteCb = true;
    stream._write(chunk, encoding, (er?: Error | null): void => onwrite(stream, er !== undefined ? er : null));
    state.sync = false;
  }
  const ret = state.length < state.highWaterMark || state.length === 0;
  if (!ret) state.needDrain = true;
  return ret && !state.destroyed && state.errored === null;
}

function doWrite(stream: Stream, state: WritableState, writev: boolean, len: int, chunk: any, chunks: { chunk: any; encoding: string }[], encoding: string, cb: ErrorCallback): void {
  state.writelen = len;
  if (cb !== nop) state.writecb = cb;
  state.writing = true;
  state.sync = true;
  state.expectWriteCb = true;
  const onw = (er?: Error | null): void => onwrite(stream, er !== undefined ? er : null);
  if (state.destroyed) onw(errDestroyed("write"));
  else if (writev) stream._writev(chunks, onw);
  else stream._write(chunk, encoding, onw);
  state.sync = false;
}

function onwriteError(stream: Stream, state: WritableState, er: Error, cb: ErrorCallback): void {
  --state.pendingcb;
  cb(er);
  errorBuffer(state);
  errorOrDestroy(stream, er, false);
}

function onwrite(stream: Stream, er: Error | null): void {
  const state = mustWritable(stream);
  if (!state.expectWriteCb) {
    errorOrDestroy(stream, errMultipleCallback(), false);
    return;
  }
  const sync = state.sync;
  const wcb = state.writecb;
  const cb: ErrorCallback = wcb !== null ? wcb : nop;
  state.writecb = null;
  state.writing = false;
  state.expectWriteCb = false;
  state.length -= state.writelen;
  state.writelen = 0;
  if (er !== null) {
    const e = er;
    if (state.errored === null) state.errored = e;
    const r = stream._readableState;
    if (r !== null && r.errored === null) r.errored = e;
    if (sync) process.nextTick((): void => onwriteError(stream, state, e, cb));
    else onwriteError(stream, state, e, cb);
  } else {
    if (state.bufferedRequestCount > 0) clearBuffer(stream, state);
    if (sync) {
      const needDrain = state.needDrain && state.length === 0;
      // (Node tests `kState & kDestroyed !== 0`, which reads its bit 0: the object mode flag)
      const needTick = needDrain || state.objectMode || cb !== nop;
      if (cb === nop) {
        if (!state.afterWritePending && needTick) {
          process.nextTick((): void => afterWrite(stream, state, 1, nop));
          state.afterWritePending = true;
        } else {
          state.pendingcb--;
          if (state.ending) finishMaybe(stream, state, true);
        }
      } else {
        const info = state.afterWriteTickInfo;
        if (info !== null && info.cb === cb) {
          info.count++;
        } else if (needTick) {
          const ni = new AfterWriteTickInfo(cb);
          state.afterWriteTickInfo = ni;
          process.nextTick((): void => {
            state.afterWriteTickInfo = null;
            afterWrite(stream, state, ni.count, ni.cb);
          });
          state.afterWritePending = true;
        } else {
          state.pendingcb--;
          if (state.ending) finishMaybe(stream, state, true);
        }
      }
    } else {
      afterWrite(stream, state, 1, cb);
    }
  }
}

function afterWrite(stream: Stream, state: WritableState, count0: int, cb: ErrorCallback): void {
  state.afterWritePending = false;
  const needDrain = !state.ending && state.needDrain && !state.destroyed && state.length === 0;
  if (needDrain) {
    state.needDrain = false;
    stream.emit("drain");
  }
  let count = count0;
  while (count-- > 0) {
    state.pendingcb--;
    cb(null);
  }
  if (state.destroyed) errorBuffer(state);
  if (state.ending) finishMaybe(stream, state, true);
}

function errorBuffer(state: WritableState): void {
  if (state.writing) return;
  for (let n = state.bufferedIndex; n < state.buffered.length; ++n) {
    const req = state.buffered[n];
    const len = state.objectMode ? 1 : chunkLength(req.chunk);
    state.length -= len;
    const e = state.errored;
    req.callback(e !== null ? e : errDestroyed("write"));
  }
  const e2 = state.errored;
  callFinishedCallbacks(state, e2 !== null ? e2 : errDestroyed("end"));
  resetBuffer(state);
}

function clearBuffer(stream: Stream, state: WritableState): void {
  if (state.destroyed || state.bufferProcessing || state.corked > 0 || state.bufferedRequestCount === 0 || !state.constructed) return;
  const objectMode = state.objectMode;
  const buffered = state.buffered;
  const bufferedLength = buffered.length - state.bufferedIndex;
  let i = state.bufferedIndex;
  state.bufferProcessing = true;
  if (bufferedLength > 1 && stream._hasWritev()) {
    state.pendingcb -= bufferedLength - 1;
    const start = i;
    const callback: ErrorCallback = state.allNoop
      ? nop
      : (err?: Error | null): void => {
          for (let n = start; n < buffered.length; ++n) buffered[n].callback(err);
        };
    const chunks: { chunk: any; encoding: string }[] = [];
    for (let n = i; n < buffered.length; n++) chunks.push({ chunk: buffered[n].chunk, encoding: buffered[n].encoding });
    doWrite(stream, state, true, state.length, null, chunks, "", callback);
    resetBuffer(state);
  } else {
    do {
      const req = buffered[i];
      i++;
      const len = objectMode ? 1 : chunkLength(req.chunk);
      doWrite(stream, state, false, len, req.chunk, [], req.encoding, req.callback);
    } while (i < buffered.length && !state.writing);
    if (i === buffered.length) resetBuffer(state);
    else if (i > 256 && i * 2 >= buffered.length) {
      state.buffered = buffered.slice(i);
      state.bufferedIndex = 0;
    } else state.bufferedIndex = i;
  }
  state.bufferProcessing = false;
}

function needFinish(state: WritableState): boolean {
  return state.ending && state.constructed && !state.destroyed && !state.finished && !state.writing && !state.errorEmitted &&
    !state.closeEmitted && state.errored === null && state.bufferedRequestCount === 0 && state.length === 0;
}

function onFinishCb(stream: Stream, state: WritableState, err: Error | null): void {
  if (state.prefinished) {
    errorOrDestroy(stream, err !== null ? err : errMultipleCallback(), false);
    return;
  }
  state.pendingcb--;
  if (err !== null) {
    callFinishedCallbacks(state, err);
    errorOrDestroy(stream, err, state.sync);
  } else if (needFinish(state)) {
    state.prefinished = true;
    stream.emit("prefinish");
    state.pendingcb++;
    process.nextTick((): void => finish(stream, state));
  }
}

function prefinish(stream: Stream, state: WritableState): void {
  if (state.prefinished || state.finalCalled) return;
  if (stream._hasFinal() && !state.destroyed) {
    state.finalCalled = true;
    state.sync = true;
    state.pendingcb++;
    try {
      stream._final((err?: Error | null): void => onFinishCb(stream, state, err !== undefined ? err : null));
    } catch (e) {
      onFinishCb(stream, state, e as Error);
    }
    state.sync = false;
  } else {
    state.finalCalled = true;
    state.prefinished = true;
    stream.emit("prefinish");
  }
}

function finishMaybe(stream: Stream, state: WritableState, sync: boolean): void {
  if (!needFinish(state)) return;
  prefinish(stream, state);
  if (state.pendingcb === 0) {
    if (sync) {
      state.pendingcb++;
      process.nextTick((): void => {
        if (needFinish(state)) finish(stream, state);
        else state.pendingcb--;
      });
    } else if (needFinish(state)) {
      state.pendingcb++;
      finish(stream, state);
    }
  }
}

function finish(stream: Stream, state: WritableState): void {
  state.pendingcb--;
  state.finished = true;
  callFinishedCallbacks(state, null);
  stream.emit("finish");
  if (state.autoDestroy) {
    const r = stream._readableState;
    const autoDestroy = r === null || (r.autoDestroy && (r.endEmitted || r.readableOff));
    if (autoDestroy) stream.destroy();
  }
}

function callFinishedCallbacks(state: WritableState, err: Error | null): void {
  if (state.onFinished.length === 0) return;
  const cbs = state.onFinished;
  state.onFinished = [];
  for (const f of cbs) f(err);
}

// ---- Readable ----

/** How pipe() ends the destination: { end: false } leaves it open. */
export interface PipeOptions {
  end?: boolean;
}

/** A source of chunks: paused (read() them, 'readable' says when) or flowing ('data' events), 'end' after the last. */
export class Readable extends Writable {
  constructor(options: StreamOptions | null = null) {
    super(options);
    if (this instanceof Duplex) return;
    this._readableState = newReadableState(options, false);
    this._takeOptions(options);
    if (this._hasConstruct()) this._startConstruct((): void => readableOnConstructed(this));
  }

  /** Adds a chunk to the read buffer (null: the end); false once the buffer is at the highWaterMark. */
  push(chunk: any, encoding: string | null = null): boolean {
    const state = mustReadable(this);
    return state.objectMode ? addChunkPushObjectMode(this, state, chunk, encoding) : addChunkPushByteMode(this, state, chunk, encoding);
  }
  /** Puts a chunk back at the front of the read buffer. */
  unshift(chunk: any, encoding: string | null = null): boolean {
    const state = mustReadable(this);
    return state.objectMode ? addChunkUnshiftObjectMode(this, state, chunk) : addChunkUnshiftByteMode(this, state, chunk, encoding);
  }
  isPaused(): boolean {
    const state = mustReadable(this);
    return state.paused || (state.hasFlowing && !state.flowingOn);
  }
  /** Chunks come as strings in this encoding from now on. */
  setEncoding(enc: string): Readable {
    const state = mustReadable(this);
    const decoder = new Decoder(enc);
    state.decoder = decoder;
    state.encoding = decoder.encoding;
    let content = "";
    for (let i = state.bufferIndex; i < state.buffer.length; i++) {
      const data: any = state.buffer[i];
      content += typeof data === "string" ? (data as string) : decoder.write(data as Uint8Array);
    }
    if (state.ended) content += decoder.end();
    state.buffer = [];
    state.bufferIndex = 0;
    if (content !== "") state.buffer.push(content);
    state.length = content.length;
    return this;
  }

  /** Takes n bytes (or characters, or one object) from the buffer, or all of it, or null when there is not enough. */
  read(n0: number | null = null): any {
    const state = mustReadable(this);
    let nan = n0 === null;
    let n: int = 0;
    if (n0 !== null) {
      const v = n0 as number;
      if (Number.isNaN(v)) nan = true;
      else n = Math.trunc(v);
    }
    const nOrigNaN = nan;
    const nOrig = n;
    if (!nan && n > state.highWaterMark) state.highWaterMark = computeNewHighWaterMark(n);
    if (nan || n !== 0) {
      state.emittedReadable = false;
      state.eofReadablePending = false;
    }
    const stateLength = state.length;
    if (!nan && n === 0 && state.needReadable &&
      ((state.highWaterMark !== 0 ? stateLength >= state.highWaterMark : stateLength > 0) || state.ended)) {
      if (stateLength === 0 && state.ended) endReadable(this);
      else emitReadable(this);
      return null;
    }
    n = howMuchToRead(nan, n, state);
    nan = false;
    if (n === 0 && state.ended) {
      if (state.length === 0) endReadable(this);
      return null;
    }
    let doRead = state.needReadable;
    if (state.length === 0 || state.length - n < state.highWaterMark) doRead = true;
    if (state.reading || state.ended || state.destroyed || state.errored !== null || !state.constructed) {
      doRead = false;
    } else if (doRead) {
      state.reading = true;
      state.sync = true;
      if (state.length === 0) state.needReadable = true;
      try {
        this._read(state.highWaterMark);
      } catch (e) {
        errorOrDestroy(this, e as Error, false);
      }
      state.sync = false;
      if (!state.reading) n = howMuchToRead(nOrigNaN, nOrig, state);
    }
    let ret: any = null;
    if (n > 0) ret = fromList(n, state);
    if (ret === null) {
      if (state.length <= state.highWaterMark) state.needReadable = true;
      n = 0;
    } else {
      state.length -= n;
      clearAwaitDrain(state);
    }
    if (state.length === 0) {
      if (!state.ended) state.needReadable = true;
      if ((nOrigNaN || nOrig !== n) && state.ended) endReadable(this);
    }
    if (ret !== null && !state.errorEmitted && !state.closeEmitted) {
      state.dataEmitted = true;
      this.emit("data", ret);
    }
    return ret;
  }

  /** Sends every chunk to dest (pausing while dest asks to wait), and ends dest at the end unless { end: false }. */
  pipe<W extends Writable>(dest: W, pipeOpts: PipeOptions | null = null): W {
    pipeTo(this, dest, pipeOpts);
    return dest;
  }
  /** Stops sending chunks to dest (to every destination when none is given). */
  unpipe(dest: Writable | null = null): Readable {
    const state = mustReadable(this);
    if (state.pipes.length === 0) return this;
    if (dest === null) {
      const dests = state.pipes;
      state.pipes = [];
      this.pause();
      for (const d of dests) d.emit("unpipe", this, new UnpipeInfo());
      return this;
    }
    const index = state.pipes.indexOf(dest);
    if (index === -1) return this;
    state.pipes.splice(index, 1);
    if (state.pipes.length === 0) this.pause();
    dest.emit("unpipe", this, new UnpipeInfo());
    return this;
  }

  /** 'data' starts the flow (unless paused by hand); 'readable' switches to paused mode and says when to read(). */
  override on(event: string, listener: Listener): Readable {
    super.on(event, listener);
    this._onAdded(event);
    return this;
  }
  override addListener(event: string, listener: Listener): Readable {
    super.addListener(event, listener);
    this._onAdded(event);
    return this;
  }
  override once(event: string, listener: Listener): Readable {
    super.once(event, listener);
    this._onAdded(event);
    return this;
  }
  private _onAdded(ev: string): void {
    const state = this._readableState;
    if (state === null) return;
    if (ev === "data") {
      state.dataListening = true;
      if (this.listenerCount("readable") > 0) state.readableListening = true;
      if (!(state.hasFlowing && !state.flowingOn)) this.resume();
    } else if (ev === "readable") {
      if (!state.endEmitted && !state.readableListening) {
        state.readableListening = true;
        state.needReadable = true;
        state.hasFlowing = true;
        state.flowingOn = false;
        state.emittedReadable = false;
        if (state.length > 0) {
          emitReadable(this);
        } else {
          if (state.eofReadablePending) {
            state.eofReadablePending = false;
            emitReadable(this);
          }
          if (!state.reading) process.nextTick((): void => { this.read(0); });
        }
      }
    }
  }
  protected override _listenerRemoved(event: string, listener: Listener): void {
    const state = this._readableState;
    if (state === null) return;
    if (event === "readable") process.nextTick((): void => updateReadableListening(this));
    else if (event === "data" && this.listenerCount("data") === 0) state.dataListening = false;
  }
  override removeListener(event: string, listener: Listener): Readable {
    super.removeListener(event, listener);
    return this;
  }
  override off(event: string, listener: Listener): Readable {
    super.removeListener(event, listener);
    return this;
  }
  override removeAllListeners(event: string | null = null): Readable {
    super.removeAllListeners(event);
    if (this._readableState !== null && (event === "readable" || event === null)) process.nextTick((): void => updateReadableListening(this));
    return this;
  }

  /** Switches to flowing mode: 'data' events (from the next tick). */
  resume(): Readable {
    const state = mustReadable(this);
    if (state.destroyed) return this;
    if (!state.flowingOn) {
      state.hasFlowing = true;
      state.flowingOn = !state.readableListening;
      if (!state.resumeScheduled) {
        state.resumeScheduled = true;
        process.nextTick((): void => resumeNT(this, state));
      }
    }
    state.hasPaused = true;
    state.paused = false;
    return this;
  }
  /** Stops the 'data' events; chunks stay in the buffer. */
  pause(): Readable {
    const state = mustReadable(this);
    if (state.destroyed) return this;
    if (!(state.hasFlowing && !state.flowingOn)) {
      state.hasFlowing = true;
      state.flowingOn = false;
      this.emit("pause");
    }
    state.hasPaused = true;
    state.paused = true;
    return this;
  }

  /** Reads the stream as an async iterable: for await (const chunk of readable). */
  async *[Symbol.asyncIterator](): AsyncGenerator<any> {
    const it = new ReadableIterator(this);
    while (true) {
      const chunk: any = await it.next();
      if (it.done) break;
      yield chunk;
    }
    it.finish();
  }

  /** Whether read() may give more: not ended, destroyed or errored. */
  get readable(): boolean {
    const r = this._readableState;
    return r !== null && !r.readableOff && !r.destroyed && !r.errorEmitted && !r.endEmitted;
  }
  set readable(v: boolean) {
    const r = this._readableState;
    if (r !== null) r.readableOff = !v;
  }
  get readableEnded(): boolean {
    const r = this._readableState;
    return r !== null && r.endEmitted;
  }
  get readableFlowing(): boolean | null {
    const r = this._readableState;
    return r !== null ? r.flowing : null;
  }
  set readableFlowing(v: boolean | null) {
    const r = this._readableState;
    if (r === null) return;
    if (v === null) {
      r.hasFlowing = false;
      r.flowingOn = false;
    } else {
      r.hasFlowing = true;
      r.flowingOn = v;
    }
  }
  get readableLength(): int {
    const r = this._readableState;
    return r !== null ? r.length : 0;
  }
  get readableHighWaterMark(): int {
    const r = this._readableState;
    return r !== null ? r.highWaterMark : 0;
  }
  get readableObjectMode(): boolean {
    const r = this._readableState;
    return r !== null && r.objectMode;
  }
  get readableEncoding(): string | null {
    const r = this._readableState;
    return r !== null ? r.encoding : null;
  }
  get readableDidRead(): boolean {
    const r = this._readableState;
    return r !== null && r.dataEmitted;
  }
  get readableAborted(): boolean {
    const r = this._readableState;
    return r !== null && !r.readableOff && (r.destroyed || r.errored !== null) && !r.endEmitted;
  }
  get readableBuffer(): any[] {
    const r = this._readableState;
    return r !== null ? r.buffer.slice(r.bufferIndex) : [];
  }

  /** A readable that gives the elements of an array (any values: object mode). */
  static __from_array(chunks: any[], options: StreamOptions | null = null): Readable {
    return new ArrayReadable(chunks, options);
  }
  /** A readable that gives what a generator (sync or async) yields, in object mode. */
  static from<T>(iterable: AsyncIterable<T>, options: StreamOptions | null = null): Readable {
    const r = new GeneratorReadable(options);
    r.start = (): void => {
      const p: Promise<void> = r.pumpFrom(iterable);
    };
    return r;
  }
}

function mustReadable(s: Stream): ReadableState {
  const r = s._readableState;
  if (r === null) throw new StreamTypeError("ERR_INVALID_STATE", "the stream is not readable");
  return r;
}

function newReadableState(opts: StreamOptions | null, isDuplex: boolean): ReadableState {
  const s = new ReadableState();
  if (opts !== null) {
    if (opts.objectMode === true) s.objectMode = true;
    if (isDuplex && opts.readableObjectMode === true) s.objectMode = true;
    if (opts.emitClose === false) s.emitClose = false;
    if (opts.autoDestroy === false) s.autoDestroy = false;
    const de = opts.defaultEncoding;
    if (de !== null && de !== undefined) s.defaultEncoding = normEncoding(de) === "utf8" ? "utf8" : de;
    const enc = opts.encoding;
    if (enc !== null && enc !== undefined && enc !== "") {
      const d = new Decoder(enc);
      s.decoder = d;
      s.encoding = d.encoding;
    }
  }
  s.highWaterMark = hwmOf(opts, 0, isDuplex, s.objectMode);
  return s;
}

function readableOnConstructed(stream: Readable): void {
  const state = stream._readableState;
  if (state !== null && state.needReadable) maybeReadMore(stream, state);
}

class UnpipeInfo {
  hasUnpiped: boolean = false;
}

function addChunkUnshiftByteMode(stream: Readable, state: ReadableState, chunk0: any, encoding0: string | null): boolean {
  if (chunk0 === null) {
    state.reading = false;
    onEofChunk(stream, state);
    return false;
  }
  let chunk: any = chunk0;
  if (typeof chunk === "string") {
    const encoding = encoding0 !== null && encoding0 !== "" ? encoding0 : state.defaultEncoding;
    const se = state.encoding;
    if (se !== encoding) {
      if (se !== null) chunk = toBuffer(chunk as string, encoding).toString(se);
      else chunk = toBuffer(chunk as string, encoding);
    }
  } else if (isBytes(chunk)) {
    chunk = asBuffer(chunk);
  } else {
    errorOrDestroy(stream, errInvalidChunk(chunk), false);
    return false;
  }
  if (chunkLength(chunk) <= 0) return canPushMore(state);
  return addChunkUnshiftValue(stream, state, chunk);
}
function addChunkUnshiftObjectMode(stream: Readable, state: ReadableState, chunk: any): boolean {
  if (chunk === null) {
    state.reading = false;
    onEofChunk(stream, state);
    return false;
  }
  return addChunkUnshiftValue(stream, state, chunk);
}
function addChunkUnshiftValue(stream: Readable, state: ReadableState, chunk: any): boolean {
  if (state.endEmitted) errorOrDestroy(stream, new StreamError("ERR_STREAM_UNSHIFT_AFTER_END_EVENT", "stream.unshift() after end event"), false);
  else if (state.destroyed || state.errored !== null) return false;
  else addChunk(stream, state, chunk, true);
  return canPushMore(state);
}

function addChunkPushByteMode(stream: Readable, state: ReadableState, chunk0: any, encoding0: string | null): boolean {
  if (chunk0 === null) {
    state.reading = false;
    onEofChunk(stream, state);
    return false;
  }
  let chunk: any = chunk0;
  let encoding: string = encoding0 !== null ? encoding0 : "";
  if (typeof chunk === "string") {
    if (encoding === "") encoding = state.defaultEncoding;
    if (state.encoding !== encoding) {
      chunk = toBuffer(chunk as string, encoding);
      encoding = "";
    }
  } else if (isBytes(chunk)) {
    chunk = asBuffer(chunk);
    encoding = "";
  } else {
    errorOrDestroy(stream, errInvalidChunk(chunk), false);
    return false;
  }
  if (chunkLength(chunk) <= 0) {
    state.reading = false;
    maybeReadMore(stream, state);
    return canPushMore(state);
  }
  if (state.ended) {
    errorOrDestroy(stream, new StreamError("ERR_STREAM_PUSH_AFTER_EOF", "stream.push() after EOF"), false);
    return false;
  }
  if (state.destroyed || state.errored !== null) return false;
  state.reading = false;
  const dec = state.decoder;
  if (dec !== null && encoding === "") {
    const s = dec.write(chunk as Uint8Array);
    chunk = s;
    if (s.length === 0) {
      maybeReadMore(stream, state);
      return canPushMore(state);
    }
  }
  addChunk(stream, state, chunk, false);
  return canPushMore(state);
}

function addChunkPushObjectMode(stream: Readable, state: ReadableState, chunk0: any, encoding: string | null): boolean {
  if (chunk0 === null) {
    state.reading = false;
    onEofChunk(stream, state);
    return false;
  }
  if (state.ended) {
    errorOrDestroy(stream, new StreamError("ERR_STREAM_PUSH_AFTER_EOF", "stream.push() after EOF"), false);
    return false;
  }
  if (state.destroyed || state.errored !== null) return false;
  state.reading = false;
  let chunk: any = chunk0;
  const dec = state.decoder;
  if (dec !== null && (encoding === null || encoding === "") && isBytes(chunk)) chunk = dec.write(chunk as Uint8Array);
  addChunk(stream, state, chunk, false);
  return canPushMore(state);
}

function canPushMore(state: ReadableState): boolean {
  return !state.ended && (state.length < state.highWaterMark || state.length === 0);
}

function clearAwaitDrain(state: ReadableState): void {
  if (state.multiAwaitDrain) state.awaitDrainSet = [];
  else state.awaitDrainWriter = null;
}

function addChunk(stream: Readable, state: ReadableState, chunk: any, addToFront: boolean): void {
  if (state.flowingOn && !state.sync && state.dataListening && state.length === 0) {
    clearAwaitDrain(state);
    state.dataEmitted = true;
    stream.emit("data", chunk);
  } else {
    state.length += state.objectMode ? 1 : chunkLength(chunk);
    if (addToFront) {
      if (state.bufferIndex > 0) {
        state.bufferIndex--;
        state.buffer[state.bufferIndex] = chunk;
      }
      else state.buffer.unshift(chunk);
    } else {
      state.buffer.push(chunk);
    }
    if (state.needReadable) emitReadable(stream);
  }
  maybeReadMore(stream, state);
}

const MAX_HWM: int = 0x40000000;
function computeNewHighWaterMark(n0: int): int {
  if (n0 > MAX_HWM) throw new StreamRangeError("ERR_OUT_OF_RANGE", `The value of "size" is out of range. It must be <= 1GiB. Received ${n0}`);
  let n: int = 1;
  while (n < n0) n *= 2;
  return n;
}

function howMuchToRead(nan: boolean, n: int, state: ReadableState): int {
  if ((!nan && n <= 0) || (state.length === 0 && state.ended)) return 0;
  if (state.objectMode) return 1;
  if (nan) {
    if (state.decoder === null && state.length > 0) return chunkLength(state.buffer[state.bufferIndex]);
    if (state.flowingOn && state.length > 0) return chunkLength(state.buffer[state.bufferIndex]);
    return state.length;
  }
  if (n <= state.length) return n;
  return state.ended ? state.length : 0;
}

function onEofChunk(stream: Readable, state: ReadableState): void {
  if (state.ended) return;
  const dec = state.decoder;
  if (dec !== null) {
    const chunk = dec.end();
    if (chunk.length > 0) {
      state.buffer.push(chunk);
      state.length += state.objectMode ? 1 : chunk.length;
    }
  }
  state.ended = true;
  if (state.sync) {
    if (state.flowingOn || state.needReadable || stream.listenerCount("readable") > 0) emitReadable(stream);
    else state.eofReadablePending = true;
  } else {
    state.needReadable = false;
    state.emittedReadable = true;
    emitReadableNT(stream);
  }
}

function emitReadable(stream: Readable): void {
  const state = mustReadable(stream);
  state.needReadable = false;
  if (!state.emittedReadable) {
    state.emittedReadable = true;
    process.nextTick((): void => emitReadableNT(stream));
  }
}

function emitReadableNT(stream: Readable): void {
  const state = mustReadable(stream);
  if (!state.destroyed && state.errored === null && (state.length > 0 || state.ended)) {
    stream.emit("readable");
    state.emittedReadable = false;
  }
  if (!state.flowingOn && !state.ended && state.length <= state.highWaterMark) state.needReadable = true;
  flow(stream);
}

function maybeReadMore(stream: Readable, state: ReadableState): void {
  if (!state.readingMore && !state.reading && state.constructed) {
    state.readingMore = true;
    process.nextTick((): void => maybeReadMoreNT(stream, state));
  }
}

function maybeReadMoreNT(stream: Readable, state: ReadableState): void {
  while (!state.reading && !state.ended && (state.length < state.highWaterMark || (state.flowingOn && state.length === 0))) {
    const len = state.length;
    stream.read(0);
    if (len === state.length) break;
  }
  state.readingMore = false;
}

function updateReadableListening(self: Readable): void {
  const state = mustReadable(self);
  state.readableListening = self.listenerCount("readable") > 0;
  if (state.hasPaused && !state.paused && state.resumeScheduled) {
    state.hasFlowing = true;
    state.flowingOn = true;
  } else if (state.dataListening) {
    self.resume();
  } else if (!state.readableListening) {
    state.hasFlowing = false;
    state.flowingOn = false;
  }
}

function resumeNT(stream: Readable, state: ReadableState): void {
  if (!state.reading) stream.read(0);
  state.resumeScheduled = false;
  stream.emit("resume");
  flow(stream);
  if (state.flowingOn && !state.reading) stream.read(0);
}

function flow(stream: Readable): void {
  const state = mustReadable(stream);
  while (state.flowingOn && stream.read() !== null) {}
}

function fromList(n0: int, state: ReadableState): any {
  if (state.length === 0) return null;
  let n = n0;
  let idx = state.bufferIndex;
  const buf = state.buffer;
  const len = buf.length;
  let ret: any = null;
  if (state.objectMode) {
    ret = buf[idx];
    buf[idx] = null;
    idx++;
  } else if (n === 0 || n >= state.length) {
    if (state.decoder !== null) {
      let s = "";
      while (idx < len) {
        s += buf[idx] as string;
        buf[idx] = null;
        idx++;
      }
      ret = s;
    } else if (len - idx === 0) {
      ret = Buffer.alloc(0);
    } else if (len - idx === 1) {
      ret = buf[idx];
      buf[idx] = null;
      idx++;
    } else {
      const parts: Uint8Array[] = [];
      while (idx < len) {
        parts.push(buf[idx] as Uint8Array);
        buf[idx] = null;
        idx++;
      }
      ret = Buffer.concat(parts);
    }
  } else {
    const first: any = buf[idx];
    const firstLength = chunkLength(first);
    if (n < firstLength) {
      if (typeof first === "string") {
        ret = (first as string).slice(0, n);
        buf[idx] = (first as string).slice(n);
      } else {
        const b = first as Uint8Array;
        ret = asBuffer(b.subarray(0, n));
        buf[idx] = asBuffer(b.subarray(n));
      }
    } else if (n === firstLength) {
      ret = first;
      buf[idx] = null;
      idx++;
    } else if (state.decoder !== null) {
      let s = "";
      while (idx < len) {
        const str = buf[idx] as string;
        const strLength = str.length;
        if (n > strLength) {
          s += str;
          n -= strLength;
          buf[idx] = null;
          idx++;
        } else {
          if (n === strLength) {
            s += str;
            buf[idx] = null;
            idx++;
          } else {
            s += str.slice(0, n);
            buf[idx] = str.slice(n);
          }
          break;
        }
      }
      ret = s;
    } else {
      const out = Buffer.allocUnsafe(n);
      const retLen = n;
      while (idx < len) {
        const data = buf[idx] as Uint8Array;
        const dataLength = data.length;
        if (n > dataLength) {
          out.set(data, retLen - n);
          n -= dataLength;
          buf[idx] = null;
          idx++;
        } else {
          if (n === dataLength) {
            out.set(data, retLen - n);
            buf[idx] = null;
            idx++;
          } else {
            out.set(data.subarray(0, n), retLen - n);
            buf[idx] = asBuffer(data.subarray(n));
          }
          break;
        }
      }
      ret = out;
    }
  }
  if (idx === len) {
    state.buffer = [];
    state.bufferIndex = 0;
  } else if (idx > 1024) {
    state.buffer = buf.slice(idx);
    state.bufferIndex = 0;
  } else {
    state.bufferIndex = idx;
  }
  return ret;
}

function endReadable(stream: Readable): void {
  const state = mustReadable(stream);
  if (!state.endEmitted && !state.endScheduled) {
    state.ended = true;
    state.endScheduled = true;
    process.nextTick((): void => endReadableNT(state, stream));
  }
}

function endReadableNT(state: ReadableState, stream: Readable): void {
  state.endScheduled = false;
  if (state.errored === null && !state.closeEmitted && !state.endEmitted && state.length === 0) {
    state.endEmitted = true;
    stream.emit("end");
    if (stream.writable && !stream.allowHalfOpen) {
      process.nextTick((): void => {
        if (stream.writable && !stream.writableEnded && !stream.destroyed) stream.end();
      });
    } else if (state.autoDestroy) {
      const w = stream._writableState;
      const autoDestroy = w === null || (w.autoDestroy && (w.finished || w.writableOff));
      if (autoDestroy) stream.destroy();
    }
  }
}

// pipe(): Node's Readable.prototype.pipe, its closures as listeners of src and dest
function pipeTo(src: Readable, dest: Writable, pipeOpts: PipeOptions | null): void {
  const state = mustReadable(src);
  if (state.pipes.length === 1 && !state.multiAwaitDrain) {
    state.multiAwaitDrain = true;
    const one = state.awaitDrainWriter;
    state.awaitDrainSet = one !== null ? [one] : [];
    state.awaitDrainWriter = null;
  }
  state.pipes.push(dest);
  const doEnd = pipeOpts === null || pipeOpts.end !== false;
  let cleanedUp = false;
  let ondrain: Listener | null = null;
  let onclose: Listener | null = null;
  let onfinish: Listener | null = null;
  let onerror: Listener | null = null;
  let onunpipe: Listener | null = null;
  let ondata: Listener | null = null;
  const unpipe: Listener = (...a: any[]): void => {
    src.unpipe(dest);
  };
  const onend: Listener = (...a: any[]): void => {
    dest.end();
  };
  const endFn: Listener = doEnd ? onend : unpipe;
  if (state.endEmitted) process.nextTick((): void => endFn());
  else src.once("end", endFn);

  const cleanup = (): void => {
    if (onclose !== null) dest.removeListener("close", onclose);
    if (onfinish !== null) dest.removeListener("finish", onfinish);
    if (ondrain !== null) dest.removeListener("drain", ondrain);
    if (onerror !== null) dest.removeListener("error", onerror);
    if (onunpipe !== null) dest.removeListener("unpipe", onunpipe);
    src.removeListener("end", onend);
    src.removeListener("end", unpipe);
    if (ondata !== null) src.removeListener("data", ondata);
    cleanedUp = true;
    const d = ondrain;
    if (d !== null && (state.multiAwaitDrain || state.awaitDrainWriter !== null)) d();
  };
  onunpipe = (...a: any[]): void => {
    const readable: any = a.length > 0 ? a[0] : null;
    const info: any = a.length > 1 ? a[1] : null;
    if (readable === src && info !== null) {
      const ui = info as UnpipeInfo;
      if (!ui.hasUnpiped) {
        ui.hasUnpiped = true;
        cleanup();
      }
    }
  };
  dest.on("unpipe", onunpipe);

  const pause = (): void => {
    if (!cleanedUp) {
      if (state.pipes.length === 1 && state.pipes[0] === dest) {
        state.awaitDrainWriter = dest;
        state.multiAwaitDrain = false;
      } else if (state.pipes.length > 1 && state.pipes.indexOf(dest) >= 0) {
        state.awaitDrainSet.push(dest);
      }
      src.pause();
    }
    if (ondrain === null) {
      const od: Listener = (...a: any[]): void => {
        if (state.awaitDrainWriter === dest) {
          state.awaitDrainWriter = null;
        } else if (state.multiAwaitDrain) {
          const i = state.awaitDrainSet.indexOf(dest);
          if (i >= 0) state.awaitDrainSet.splice(i, 1);
        }
        const empty = state.multiAwaitDrain ? state.awaitDrainSet.length === 0 : state.awaitDrainWriter === null;
        if (empty && state.dataListening) src.resume();
      };
      ondrain = od;
      dest.on("drain", od);
    }
  };

  ondata = (...a: any[]): void => {
    const chunk: any = a[0];
    try {
      const ret = dest.write(chunk);
      if (ret === false) pause();
    } catch (e) {
      dest.destroy(e as Error);
    }
  };
  src.on("data", ondata);

  onerror = (...a: any[]): void => {
    const er = a[0] as Error;
    unpipe();
    if (onerror !== null) dest.removeListener("error", onerror);
    if (dest.listenerCount("error") === 0) {
      const w = dest._writableState;
      const r = dest._readableState;
      const errEmitted = w !== null ? w.errorEmitted : r !== null ? r.errorEmitted : true;
      if ((w !== null || r !== null) && !errEmitted) errorOrDestroy(dest, er, false);
      else dest.emit("error", er);
    }
  };
  dest.prependListener("error", onerror);

  onclose = (...a: any[]): void => {
    if (onfinish !== null) dest.removeListener("finish", onfinish);
    unpipe();
  };
  dest.once("close", onclose);
  onfinish = (...a: any[]): void => {
    if (onclose !== null) dest.removeListener("close", onclose);
    unpipe();
  };
  dest.once("finish", onfinish);

  dest.emit("pipe", src);
  if (dest.writableNeedDrain) pause();
  else if (!state.flowingOn) src.resume();
}

// ---- async iteration (Node's createAsyncIterator, as one waiting reader) ----

class ReadableIterator {
  stream: Readable;
  done: boolean = false;
  private started: boolean = false;
  private error: Error | null = null;
  private ended: boolean = false;
  private wake: (() => void) | null = null;
  private onReadable: Listener;
  private cleanupEos: (() => void) | null = null;
  constructor(stream: Readable) {
    this.stream = stream;
    this.onReadable = (...a: any[]): void => this.wakeup();
  }
  private wakeup(): void {
    const w = this.wake;
    this.wake = null;
    if (w !== null) w();
  }
  private start(): void {
    this.started = true;
    this.stream.on("readable", this.onReadable);
    this.cleanupEos = eos(this.stream, { writable: false }, (err?: Error | null): void => {
      if (err !== undefined && err !== null) this.error = aggregateTwoErrors(this.error, err);
      else this.ended = true;
      this.wakeup();
    });
  }
  async next(): Promise<any> {
    if (!this.started) this.start();
    while (true) {
      const chunk: any = this.stream.destroyed ? null : this.stream.read();
      if (chunk !== null) return chunk;
      const e = this.error;
      if (e !== null) {
        this.done = true;
        this.finish();
        throw e;
      }
      if (this.ended) {
        this.done = true;
        return null;
      }
      await new Promise<void>((resolve) => {
        this.wake = (): void => resolve();
      });
    }
  }
  /** The loop ended (all read, a break, an error): the stream is destroyed (Node's destroyOnReturn). */
  finish(): void {
    if (this.cleanupEos === null) return;
    const s = this.stream;
    const st = s._readableState;
    if (this.error !== null || st === null || st.autoDestroy) destroyer(s, null);
    else {
      s.removeListener("readable", this.onReadable);
      const c = this.cleanupEos;
      c();
    }
    this.cleanupEos = null;
  }
}

// ---- Readable.from ----

class ArrayReadable extends Readable {
  private items: any[];
  private pos: int = 0;
  private reading2: boolean = false;
  constructor(items: any[], options: StreamOptions | null) {
    super(fromOptions(options));
    this.items = items;
  }
  override _read(size: number): void {
    if (this.reading2) return;
    this.reading2 = true;
    while (true) {
      if (this.pos >= this.items.length) {
        this.push(null);
        return;
      }
      const v: any = this.items[this.pos++];
      if (v === null) {
        this.reading2 = false;
        this.destroy(new StreamTypeError("ERR_STREAM_NULL_VALUES", "May not write null values to stream"));
        return;
      }
      if (this.push(v)) continue;
      this.reading2 = false;
      return;
    }
  }
  override _destroy(error: Error | null, callback: ErrorCallback): void {
    // (Node: the iterator's close() is an async function, then the callback on the next tick)
    Promise.resolve().then((): void => {
      process.nextTick((): void => callback(error));
    });
  }
}

function fromOptions(options: StreamOptions | null): StreamOptions {
  const o: StreamOptions = { objectMode: true, highWaterMark: 1 };
  if (options !== null) {
    if (options.objectMode !== null && options.objectMode !== undefined) o.objectMode = options.objectMode;
    if (options.highWaterMark !== null && options.highWaterMark !== undefined) o.highWaterMark = options.highWaterMark;
    if (options.encoding !== null && options.encoding !== undefined) o.encoding = options.encoding;
    if (options.emitClose !== null && options.emitClose !== undefined) o.emitClose = options.emitClose;
    if (options.autoDestroy !== null && options.autoDestroy !== undefined) o.autoDestroy = options.autoDestroy;
  }
  return o;
}

class GeneratorReadable extends Readable {
  start: (() => void) | null = null;
  private started: boolean = false;
  private waiting: (() => void) | null = null;
  private stopped: boolean = false;
  constructor(options: StreamOptions | null) {
    super(fromOptions(options));
  }
  override _read(size: number): void {
    if (!this.started) {
      this.started = true;
      const s = this.start;
      this.start = null;
      if (s !== null) s();
      return;
    }
    const w = this.waiting;
    this.waiting = null;
    if (w !== null) w();
  }
  async pumpFrom<T>(iterable: AsyncIterable<T>): Promise<void> {
    try {
      for await (const v of iterable) {
        if (this.stopped) break;
        const value: any = v;
        if (value === null) throw new StreamTypeError("ERR_STREAM_NULL_VALUES", "May not write null values to stream");
        if (!this.push(value)) {
          await new Promise<void>((resolve) => {
            this.waiting = (): void => resolve();
          });
          if (this.stopped) break;
        }
      }
      if (!this.stopped) this.push(null);
    } catch (e) {
      this.destroy(e as Error);
    }
  }
  override _destroy(error: Error | null, callback: ErrorCallback): void {
    this.stopped = true;
    const w = this.waiting;
    this.waiting = null;
    if (w !== null) w();
    Promise.resolve().then((): void => {
      Promise.resolve().then((): void => {
        Promise.resolve().then((): void => {
          process.nextTick((): void => callback(error));
        });
      });
    });
  }
}

// ---- Duplex, Transform, PassThrough ----

/** Both a Readable and a Writable: two independent sides in one object. */
export class Duplex extends Readable {
  constructor(options: StreamOptions | null = null) {
    super(options);
    this._readableState = newReadableState(options, true);
    this._writableState = newWritableState(options, true);
    const r = this._readableState as ReadableState;
    const w = this._writableState as WritableState;
    if (options !== null) {
      this.allowHalfOpen = options.allowHalfOpen !== false;
      if (options.readable === false) {
        r.readableOff = true;
        r.ended = true;
        r.endEmitted = true;
      }
      if (options.writable === false) {
        w.writableOff = true;
        w.ending = true;
        w.ended = true;
        w.finished = true;
      }
    }
    this._takeOptions(options);
    if (this._hasConstruct()) {
      this._startConstruct((): void => {
        readableOnConstructed(this);
        writableOnConstructed(this);
      });
    }
  }
}

/** A Duplex whose output is computed from its input: _transform(chunk, encoding, callback) per chunk, _flush at the end. */
export class Transform extends Duplex {
  private _kCallback: ErrorCallback | null = null;
  private _optTransform: ((chunk: any, encoding: string, callback: TransformCallback) => void) | null = null;
  private _optFlush: ((callback: TransformCallback) => void) | null = null;
  private _finalRan: boolean = false;

  constructor(options: StreamOptions | null = null) {
    super(transformOptions(options));
    (this._readableState as ReadableState).sync = false;
    if (options !== null) {
      const t = options.transform;
      if (t !== null && t !== undefined) this._optTransform = t;
      const f = options.flush;
      if (f !== null && f !== undefined) this._optFlush = f;
    }
    this.on("prefinish", (...a: any[]): void => {
      if (!this._finalRan) this._transformFinal(null);
    });
  }

  /** What a subclass does with each chunk: push() the output (or give it to the callback), then callback(). */
  _transform(chunk: any, encoding: string, callback: TransformCallback): void {
    const f = this._optTransform;
    if (f === null) throw errNotImplemented("_transform()");
    __xs_this_set(this);
    f(chunk, encoding, callback);
    __xs_this_set(null);
  }
  /** What a subclass pushes at the end, before the readable side ends. */
  _flush(callback: TransformCallback): void {
    const f = this._optFlush;
    if (f === null) {
      callback(null);
      return;
    }
    __xs_this_set(this);
    f(callback);
    __xs_this_set(null);
  }
  private _hasFlush(): boolean {
    return this._optFlush !== null || __xs_overrides(this as Transform, "_flush");
  }

  private _transformFinal(cb: ErrorCallback | null): void {
    this._finalRan = true;
    if (this._hasFlush() && !this.destroyed) {
      this._flush((er?: Error | null, data?: any): void => {
        if (er !== undefined && er !== null) {
          if (cb !== null) cb(er);
          else this.destroy(er);
          return;
        }
        if (data !== undefined && data !== null) this.push(data);
        this.push(null);
        if (cb !== null) cb(null);
      });
    } else {
      this.push(null);
      if (cb !== null) cb(null);
    }
  }
  override _final(callback: ErrorCallback): void {
    this._transformFinal(callback);
  }

  override _write(chunk: any, encoding: string, callback: ErrorCallback): void {
    const rState = this._readableState as ReadableState;
    const wState = this._writableState as WritableState;
    const length = rState.length;
    this._transform(chunk, encoding, (err?: Error | null, val?: any): void => {
      if (err !== undefined && err !== null) {
        callback(err);
        return;
      }
      if (val !== undefined && val !== null) this.push(val);
      if (rState.ended) {
        process.nextTick((): void => callback(null));
      } else if (wState.ended || length === rState.length || rState.length < rState.highWaterMark) {
        callback(null);
      } else {
        this._kCallback = callback;
      }
    });
  }
  override _read(size: number): void {
    const cb = this._kCallback;
    if (cb !== null) {
      this._kCallback = null;
      cb(null);
    }
  }
}

function transformOptions(options: StreamOptions | null): StreamOptions | null {
  if (options === null) return null;
  const rh = hwmOf(options, 0, true, options.objectMode === true || options.readableObjectMode === true);
  if (rh === 0 && (options.highWaterMark === 0 || options.readableHighWaterMark === 0)) {
    const o: StreamOptions = { ...options };
    o.highWaterMark = null;
    o.readableHighWaterMark = 0;
    const wh = options.writableHighWaterMark;
    o.writableHighWaterMark = wh !== null && wh !== undefined ? wh : 0;
    return o;
  }
  return options;
}

/** A Transform that gives every chunk through unchanged. */
export class PassThrough extends Transform {
  constructor(options: StreamOptions | null = null) {
    super(options);
  }
  override _transform(chunk: any, encoding: string, callback: TransformCallback): void {
    callback(null, chunk);
  }
}

// ---- the tests Node's stream utilities make (internal/streams/utils) ----

function isReadableNodeStream(s: Stream): boolean {
  const r = s._readableState;
  return r !== null && !(s._writableState !== null && r.readableOff);
}
function isWritableNodeStream(s: Stream): boolean {
  const w = s._writableState;
  return w !== null && !(s._readableState !== null && w.writableOff);
}
function isDestroyedS(s: Stream): boolean {
  if (s.destroyed) return true;
  const w = s._writableState;
  const st = w !== null ? w.destroyed : s._readableState !== null && (s._readableState as ReadableState).destroyed;
  return st;
}
function isReadableFinished(s: Stream, strict: boolean): boolean {
  if (!isReadableNodeStream(s)) return false;
  const r = s._readableState as ReadableState;
  if (r.errored !== null) return false;
  return r.endEmitted || (!strict && r.ended && r.length === 0);
}
function isWritableFinished(s: Stream, strict: boolean): boolean {
  if (!isWritableNodeStream(s)) return false;
  const w = s._writableState as WritableState;
  if (w.finished) return true;
  if (w.errored !== null) return false;
  return w.finished || (!strict && w.ended && w.length === 0);
}
function isWritableEnded(s: Stream): boolean {
  if (!isWritableNodeStream(s)) return false;
  const w = s._writableState as WritableState;
  if (w.ending) return true;
  if (w.errored !== null) return false;
  return w.ended;
}
/** Node's isReadable: 1 true, 0 false, -1 null (a stream without a readable side). */
function isReadableT(s: Stream): int {
  if (s._readableState === null) return -1;
  if (isDestroyedS(s)) return 0;
  return isReadableNodeStream(s) && s instanceof Readable && (s as Readable).readable && !isReadableFinished(s, true) ? 1 : 0;
}
/** Node's isWritable: 1 true, 0 false, -1 null (a stream without a writable side). */
function isWritableT(s: Stream): int {
  if (s._writableState === null) return -1;
  if (isDestroyedS(s)) return 0;
  return isWritableNodeStream(s) && (s as Writable).writable && !isWritableEnded(s) ? 1 : 0;
}
function isFinishedS(s: Stream, readable: boolean, writable: boolean): boolean {
  if (isDestroyedS(s)) return true;
  if (readable && isReadableT(s) === 1) return false;
  if (writable && isWritableT(s) === 1) return false;
  return true;
}
function willEmitCloseS(s: Stream): boolean {
  const w = s._writableState;
  if (w !== null) return w.autoDestroy && w.emitClose && !w.closed;
  const r = s._readableState;
  return r !== null && r.autoDestroy && r.emitClose && !r.closed;
}
function isClosedS(s: Stream): boolean {
  return s.closed;
}
function getEosErrored(s: Stream): Error | null {
  const w = s._writableState;
  if (w !== null && w.errored !== null) return w.errored;
  const r = s._readableState;
  return r !== null ? r.errored : null;
}

/** Whether a stream is readable (not ended, destroyed or errored); null for a stream with no readable side. */
export function isReadable(stream: Stream): boolean | null {
  const t = isReadableT(stream);
  return t < 0 ? null : t === 1;
}
/** Whether a stream is writable (not ended, destroyed or errored); null for a stream with no writable side. */
export function isWritable(stream: Stream): boolean | null {
  const t = isWritableT(stream);
  return t < 0 ? null : t === 1;
}
/** Whether a stream was destroyed or emitted an error. */
export function isErrored(stream: Stream): boolean {
  const r = stream._readableState;
  const w = stream._writableState;
  return (r !== null && (r.errorEmitted || r.errored !== null)) || (w !== null && (w.errorEmitted || w.errored !== null));
}
/** Whether a readable was read from or aborted. */
export function isDisturbed(stream: Stream): boolean {
  const r = stream._readableState;
  return r !== null && (r.dataEmitted || ((r.destroyed || r.errored !== null) && !r.endEmitted));
}

// ---- finished (end-of-stream) ----

/** What finished() waits for: { readable: false } ignores the readable side, { writable: false } the writable one;
 * { error: false } does not listen for 'error'. */
export interface FinishedOptions {
  readable?: boolean;
  writable?: boolean;
  error?: boolean;
  cleanup?: boolean;
}

function getEosOnCloseError(s: Stream, readable: boolean, readableFinished: boolean, writable: boolean, writableFinished: boolean): Error | null {
  const errored = getEosErrored(s);
  if (errored !== null) return errored;
  if (readable && !readableFinished && isReadableNodeStream(s) && !isReadableFinished(s, false)) return errPrematureClose();
  if (writable && !writableFinished && !isWritableFinished(s, false)) return errPrematureClose();
  return null;
}

/** Node's eos(): callback once the stream is done (ended, finished, closed) or failed; returns the cleanup. */
function eos(stream: Stream, options: FinishedOptions, callback0: ErrorCallback): () => void {
  const ro = options.readable;
  const wo = options.writable;
  const readable = ro !== null && ro !== undefined ? ro : isReadableNodeStream(stream);
  const writable = wo !== null && wo !== undefined ? wo : isWritableNodeStream(stream);
  let willEmitClose = willEmitCloseS(stream) && isReadableNodeStream(stream) === readable && isWritableNodeStream(stream) === writable;
  let writableFinished = isWritableFinished(stream, false);
  let readableFinished = isReadableFinished(stream, false);
  const wState = stream._writableState;
  const rState = stream._readableState;
  let immediate = false;
  let immediateErr: Error | null = null;
  if (isClosedS(stream)) {
    immediate = true;
    immediateErr = getEosOnCloseError(stream, readable, readableFinished, writable, writableFinished);
  } else if ((wState !== null && wState.errorEmitted) || (rState !== null && rState.errorEmitted)) {
    if (!willEmitClose) {
      immediate = true;
      immediateErr = getEosErrored(stream);
    }
  } else if (!readable && (!willEmitClose || isReadableT(stream) === 1) && (writableFinished || isWritableT(stream) === 0) &&
    (wState === null || wState.pendingcb === 0)) {
    immediate = true;
    immediateErr = getEosErrored(stream);
  } else if (!writable && (!willEmitClose || isWritableT(stream) === 1) && (readableFinished || isReadableT(stream) === 0)) {
    immediate = true;
    immediateErr = getEosErrored(stream);
  }
  let callback: ErrorCallback = callback0;
  const noErrorListener: Listener = (...a: any[]): void => {};
  if (immediate) {
    let cleanup0 = (): void => {
      callback = nop;
    };
    if (options.error !== false) {
      stream.on("error", noErrorListener);
      cleanup0 = (): void => {
        callback = nop;
        stream.removeListener("error", noErrorListener);
      };
    }
    const e = immediateErr;
    process.nextTick((): void => {
      if (e === null) callback();
      else callback(e);
    });
    return cleanup0;
  }
  let called = false;
  const call = (err: Error | null): void => {
    if (called) return;
    called = true;
    if (err === null) callback();
    else callback(err);
  };
  const onfinish: Listener = (...a: any[]): void => {
    writableFinished = true;
    if (stream.destroyed) willEmitClose = false;
    if (willEmitClose && (!(stream instanceof Readable && (stream as Readable).readable) || readable)) return;
    if (!readable || readableFinished) call(null);
  };
  const onend: Listener = (...a: any[]): void => {
    readableFinished = true;
    if (stream.destroyed) willEmitClose = false;
    if (willEmitClose && (!(stream as Writable).writable || writable)) return;
    if (!writable || writableFinished) call(null);
  };
  const onerror: Listener = (...a: any[]): void => {
    call(a.length > 0 && a[0] !== null ? (a[0] as Error) : null);
  };
  const onclose: Listener = (...a: any[]): void => {
    call(getEosOnCloseError(stream, readable, readableFinished, writable, writableFinished));
  };
  stream.on("end", onend);
  stream.on("finish", onfinish);
  if (options.error !== false) stream.on("error", onerror);
  stream.on("close", onclose);
  return (): void => {
    callback = nop;
    stream.removeListener("finish", onfinish);
    stream.removeListener("end", onend);
    stream.removeListener("error", onerror);
    stream.removeListener("close", onclose);
  };
}

/** Calls callback once the stream is done: ended (readable), finished (writable), or failed (the error); returns a
 * function that removes the listeners. finished(stream, cb) and finished(stream, options, cb) as in Node. */
export function finished(stream: Stream, options: FinishedOptions | ErrorCallback | null, callback: ErrorCallback | null = null): () => void {
  let opts: FinishedOptions = {};
  let cb: ErrorCallback = nop;
  if (options === null) {
    // (finished(stream, null, cb))
  } else if (typeof options === "function") {
    cb = options as ErrorCallback;
  } else {
    opts = options as FinishedOptions;
  }
  if (callback !== null) cb = callback;
  return eos(stream, opts, cb);
}

/** finished() as a promise (stream/promises). */
export function __finishedPromise(stream: Stream, options: FinishedOptions | null): Promise<void> {
  const opts: FinishedOptions = options !== null ? options : {};
  return new Promise<void>((resolve, reject) => {
    let cleanup: (() => void) | null = null;
    cleanup = eos(stream, opts, (err?: Error | null): void => {
      if (opts.cleanup === true && cleanup !== null) cleanup();
      if (err !== undefined && err !== null) reject(err);
      else resolve();
    });
  });
}

/** Node's destroyImpl.destroyer: destroy(err), or an AbortError for a stream that is not done. */
function destroyer(stream: Stream, err0: Error | null): void {
  if (isDestroyedS(stream)) return;
  let err = err0;
  if (err === null && !isFinishedS(stream, true, true)) err = new AbortError();
  stream.destroy(err);
}

// ---- pipeline ----

/** What pipeline() calls at the end: the first error, or nothing. */
export type PipelineCallback = (err?: Error | null) => void;

class PipeDestroyer {
  finished: boolean = false;
  stream: Stream;
  cleanup: () => void;
  constructor(stream: Stream, reading: boolean, writing: boolean) {
    this.stream = stream;
    stream.on("close", (...a: any[]): void => {
      this.finished = true;
    });
    this.cleanup = eos(stream, { readable: reading, writable: writing }, (err?: Error | null): void => {
      this.finished = err === undefined || err === null;
    });
  }
  destroy(err: Error | null): void {
    if (this.finished) return;
    this.finished = true;
    destroyer(this.stream, err !== null ? err : errDestroyed("pipe"));
  }
}

/** Pipes the streams into each other (source, transforms..., destination), destroys them all on an error, and calls
 * the callback (last argument) once the destination is done or something failed. Returns the last stream. */
export function pipeline(...args: (Stream | PipelineCallback)[]): Stream {
  const streams: Stream[] = [];
  let cb: PipelineCallback | null = null;
  for (const a of args) {
    if (a instanceof Stream) streams.push(a);
    else cb = a;
  }
  if (cb === null) throw new StreamTypeError("ERR_INVALID_ARG_TYPE", 'The "streams[stream.length - 1]" argument must be of type function. Received an instance of Stream');
  const user = cb;
  let called = false;
  return pipelineImpl(streams, (err?: Error | null): void => {
    if (called) return;
    called = true;
    if (err !== undefined && err !== null) user(err);
    else user();
  }, true);
}

/** pipeline() as a promise (stream/promises). */
export function __pipelinePromise(streams: Stream[], end: boolean): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    pipelineImpl(streams, (err?: Error | null): void => {
      if (err !== undefined && err !== null) reject(err);
      else resolve();
    }, end);
  });
}

function pipelineImpl(streams: Stream[], callback: ErrorCallback, endLast: boolean): Stream {
  if (streams.length < 2) throw new StreamTypeError("ERR_MISSING_ARGS", 'The "streams" argument must be specified');
  let error: Error | null = null;
  const destroys: PipeDestroyer[] = [];
  const lastStreamCleanup: (() => void)[] = [];
  let finishCount = 0;
  let done = false;
  const finishImpl = (err: Error | null, final: boolean): void => {
    if (err !== null) {
      const cur = error;
      if (cur === null || errorCode(cur) === "ERR_STREAM_PREMATURE_CLOSE" || cur.name === "AbortError") error = err;
    }
    if (error === null && !final) return;
    while (destroys.length > 0) {
      const d = destroys[0];
      destroys.splice(0, 1);
      d.destroy(error);
    }
    if (final && !done) {
      done = true;
      if (error === null) for (const f of lastStreamCleanup) f();
      const e = error;
      process.nextTick((): void => {
        if (e === null) callback();
        else callback(e);
      });
    }
  };
  const finishOne = (err?: Error | null): void => {
    finishCount--;
    finishImpl(err !== undefined ? err : null, finishCount === 0);
  };
  const finishOnlyHandleError = (err?: Error | null): void => {
    finishImpl(err !== undefined ? err : null, false);
  };
  let ret: Stream = streams[0];
  for (let i = 0; i < streams.length; i++) {
    const stream = streams[i];
    const reading = i < streams.length - 1;
    const writing = i > 0;
    const next: Stream | null = i + 1 < streams.length ? streams[i + 1] : null;
    const end = reading || endLast;
    const isLastStream = i === streams.length - 1;
    if (next !== null && (next.closed || next.destroyed)) {
      throw new StreamError("ERR_STREAM_UNABLE_TO_PIPE", "Cannot pipe to a closed or destroyed stream");
    }
    if (end) {
      const d = new PipeDestroyer(stream, reading, writing);
      destroys.push(d);
      if (isReadableT(stream) === 1 && isLastStream) lastStreamCleanup.push(d.cleanup);
    }
    const onError: Listener = (...a: any[]): void => {
      const err: Error | null = a.length > 0 && a[0] !== null ? (a[0] as Error) : null;
      if (err !== null && err.name !== "AbortError" && errorCode(err) !== "ERR_STREAM_PREMATURE_CLOSE") finishOnlyHandleError(err);
    };
    stream.on("error", onError);
    if (isReadableT(stream) === 1 && isLastStream) {
      lastStreamCleanup.push((): void => {
        stream.removeListener("error", onError);
      });
    }
    if (i > 0) {
      if (!isReadableNodeStream(ret)) {
        throw new StreamTypeError("ERR_INVALID_ARG_TYPE", 'The "val" argument must be an instance of Readable, Iterable, AsyncIterable, ReadableStream, or TransformStream.');
      }
      finishCount += 2;
      const cleanup = pipelineLink(ret as Readable, stream as Writable, finishOne, finishOnlyHandleError, end);
      if (isReadableT(stream) === 1 && isLastStream) lastStreamCleanup.push(cleanup);
      ret = stream;
    }
  }
  return ret;
}

function pipelineLink(src: Readable, dst: Writable, fin: ErrorCallback, finishOnlyHandleError: ErrorCallback, end: boolean): () => void {
  let ended = false;
  dst.on("close", (...a: any[]): void => {
    if (!ended) finishOnlyHandleError(errPrematureClose());
  });
  src.pipe(dst, { end: false });
  if (end) {
    const endFn = (): void => {
      ended = true;
      dst.end();
    };
    if (isReadableFinished(src, true)) process.nextTick(endFn);
    else src.once("end", (...a: any[]): void => endFn());
  } else {
    fin();
  }
  eos(src, { readable: true, writable: false }, (err?: Error | null): void => {
    const rState = src._readableState;
    if (err !== undefined && err !== null && errorCode(err) === "ERR_STREAM_PREMATURE_CLOSE" && rState !== null && rState.ended &&
      rState.errored === null && !rState.errorEmitted) {
      src.once("end", (...a: any[]): void => fin());
      src.once("error", (...a: any[]): void => fin(a[0] as Error));
    } else if (err !== undefined && err !== null) {
      fin(err);
    } else {
      fin();
    }
  });
  return eos(dst, { readable: false, writable: true }, fin);
}

/** Node's stream.promises: pipeline and finished as promises. */
export class promises {
  static pipeline(...streams: Stream[]): Promise<void> {
    return __pipelinePromise(streams, true);
  }
  static finished(stream: Stream, options: FinishedOptions | null = null): Promise<void> {
    return __finishedPromise(stream, options);
  }
}

// ---- fs.createReadStream / fs.createWriteStream ----

/** The options of fs.createReadStream and fs.createWriteStream. */
export interface FsStreamOptions {
  flags?: string;
  encoding?: string;
  fd?: int;
  mode?: int;
  autoClose?: boolean;
  emitClose?: boolean;
  start?: int;
  end?: int;
  highWaterMark?: int;
  flush?: boolean;
}

class FsError extends Error {
  code: string;
  errno: int;
  syscall: string;
  path: string;
  constructor(e: int, syscall: string, path: string) {
    const code = fs_stream_errcode(e);
    const text = fs_stream_errcode(-e);
    super(path !== "" ? `${code}: ${text}, ${syscall} '${path}'` : `${code}: ${text}, ${syscall}`);
    this.code = code;
    this.errno = -e;
    this.syscall = syscall;
    this.path = path;
  }
}
declare function fs_stream_read(fd: int, b: Uint8Array, off: int, len: int, pos: int): int;
declare function fs_stream_close(fd: int): int;
declare function fs_stream_errcode(e: int): string;

function fsOpen(stream: Stream, path: string, flags: string, mode: int, setFd: (fd: int) => void, callback: ErrorCallback): void {
  // (Node opens on the thread pool: the result comes on a later turn of the loop)
  setImmediate((): void => {
    const fd = fs_stream_open(path, flags, mode);
    if (fd < 0) {
      callback(new FsError(-fd, "open", path));
      return;
    }
    setFd(fd);
    callback(null);
    stream.emit("open", fd);
    stream.emit("ready");
  });
}
function fsClose(fd: int, err: Error | null, cb: ErrorCallback): void {
  setImmediate((): void => {
    const r = fs_stream_close(fd);
    if (err !== null) cb(err);
    else if (r < 0) cb(new FsError(-r, "close", ""));
    else cb(null);
  });
}

/** A file read as a stream (fs.createReadStream): 'open' (the fd), 'ready', the chunks, 'end', 'close'. */
export class ReadStream extends Readable {
  path: string;
  fd: int = -1;
  flags: string = "r";
  mode: int = 0o666;
  start: int = -1;
  end: number = Infinity;
  bytesRead: int = 0;
  private pos: int = -1;
  private performingIO: boolean = false;
  private ioDone: (() => void) | null = null;
  constructor(path: string, options: FsStreamOptions | null = null) {
    super(fsReadOptions(options));
    this.path = path;
    if (options !== null) {
      const fl = options.flags;
      if (fl !== null && fl !== undefined) this.flags = fl;
      const md = options.mode;
      if (md !== null && md !== undefined) this.mode = md;
      const fd = options.fd;
      if (fd !== null && fd !== undefined) this.fd = fd;
      const st = options.start;
      if (st !== null && st !== undefined) {
        this.start = st;
        this.pos = st;
      }
      const en = options.end;
      if (en !== null && en !== undefined) {
        this.end = en;
        if (this.start >= 0 && this.start > en) {
          throw new StreamRangeError("ERR_OUT_OF_RANGE", `The value of "start" is out of range. It must be <= "end" (here: ${en}). Received ${this.start}`);
        }
      }
    }
  }
  /** true until the file is open. */
  get pending(): boolean {
    return this.fd < 0;
  }
  override _construct(callback: ErrorCallback): void {
    if (this.fd >= 0) {
      callback();
      return;
    }
    fsOpen(this, this.path, this.flags, this.mode, (fd: int): void => {
      this.fd = fd;
    }, callback);
  }
  override _read(size: number): void {
    let n: number = this.pos >= 0 ? Math.min(this.end - this.pos + 1, size) : Math.min(this.end - this.bytesRead + 1, size);
    if (n <= 0) {
      this.push(null);
      return;
    }
    const want: int = Math.trunc(n);
    this.performingIO = true;
    setImmediate((): void => {
      const buf = Buffer.allocUnsafe(want);
      const r = this.destroyed ? 0 : fs_stream_read(this.fd, buf, 0, want, this.pos);
      this.performingIO = false;
      if (this.destroyed) {
        const d = this.ioDone;
        this.ioDone = null;
        if (d !== null) d();
        return;
      }
      if (r < 0) {
        errorOrDestroy(this, new FsError(-r, "read", ""), false);
      } else if (r > 0) {
        if (this.pos >= 0) this.pos += r;
        this.bytesRead += r;
        this.push(r === want ? buf : Buffer.from(buf.subarray(0, r)));
      } else {
        this.push(null);
      }
    });
  }
  override _destroy(err: Error | null, cb: ErrorCallback): void {
    if (this.performingIO) {
      this.ioDone = (): void => this.closeFd(err, cb);
    } else {
      this.closeFd(err, cb);
    }
  }
  private closeFd(err: Error | null, cb: ErrorCallback): void {
    if (this.fd < 0) {
      cb(err);
      return;
    }
    const fd = this.fd;
    this.fd = -1;
    fsClose(fd, err, cb);
  }
  /** Destroys the stream (closing the file); cb once it is closed. */
  close(cb: ErrorCallback | null = null): void {
    if (cb !== null) finished(this, cb);
    this.destroy();
  }
}

function fsReadOptions(options: FsStreamOptions | null): StreamOptions {
  const o: StreamOptions = { highWaterMark: 64 * 1024 };
  if (options !== null) {
    const h = options.highWaterMark;
    if (h !== null && h !== undefined) o.highWaterMark = h;
    const e = options.encoding;
    if (e !== null && e !== undefined) o.encoding = e;
    if (options.emitClose === false) o.emitClose = false;
    o.autoDestroy = options.autoClose !== false;
  }
  return o;
}

/** A file written as a stream (fs.createWriteStream): 'open', 'ready', then the writes, 'finish', 'close'. */
export class WriteStream extends Writable {
  path: string;
  fd: int = -1;
  flags: string = "w";
  mode: int = 0o666;
  start: int = -1;
  bytesWritten: int = 0;
  private pos: int = -1;
  private performingIO: boolean = false;
  private ioDone: (() => void) | null = null;
  constructor(path: string, options: FsStreamOptions | null = null) {
    super(fsWriteOptions(options));
    this.path = path;
    if (options !== null) {
      const fl = options.flags;
      if (fl !== null && fl !== undefined) this.flags = fl;
      const md = options.mode;
      if (md !== null && md !== undefined) this.mode = md;
      const fd = options.fd;
      if (fd !== null && fd !== undefined) this.fd = fd;
      const st = options.start;
      if (st !== null && st !== undefined) {
        this.start = st;
        this.pos = st;
      }
      const enc = options.encoding;
      if (enc !== null && enc !== undefined) this.setDefaultEncoding(enc);
    }
  }
  get pending(): boolean {
    return this.fd < 0;
  }
  override _construct(callback: ErrorCallback): void {
    if (this.fd >= 0) {
      callback();
      return;
    }
    fsOpen(this, this.path, this.flags, this.mode, (fd: int): void => {
      this.fd = fd;
    }, callback);
  }
  private writeAll(data: Uint8Array, cb: ErrorCallback): void {
    this.performingIO = true;
    const pos = this.pos;
    if (this.pos >= 0) this.pos += data.length;
    setImmediate((): void => {
      let err: Error | null = null;
      let off = 0;
      while (off < data.length && !this.destroyed) {
        const r = fs_stream_write(this.fd, data, off, data.length - off, pos >= 0 ? pos + off : -1);
        if (r < 0) {
          err = new FsError(-r, "write", "");
          break;
        }
        this.bytesWritten += r;
        off += r;
      }
      if (err === null && this.destroyed) err = errDestroyed("write");
      this.performingIO = false;
      cb(err);
      if (this.destroyed) {
        const d = this.ioDone;
        this.ioDone = null;
        if (d !== null) d();
      }
    });
  }
  override _write(chunk: any, encoding: string, callback: ErrorCallback): void {
    this.writeAll(chunk as Uint8Array, callback);
  }
  override _writev(chunks: { chunk: any; encoding: string }[], callback: ErrorCallback): void {
    const parts: Uint8Array[] = [];
    for (const c of chunks) parts.push(c.chunk as Uint8Array);
    this.writeAll(Buffer.concat(parts), callback);
  }
  override _destroy(err: Error | null, cb: ErrorCallback): void {
    if (this.performingIO) {
      this.ioDone = (): void => this.closeFd(err, cb);
    } else {
      this.closeFd(err, cb);
    }
  }
  private closeFd(err: Error | null, cb: ErrorCallback): void {
    if (this.fd < 0) {
      cb(err);
      return;
    }
    const fd = this.fd;
    this.fd = -1;
    fsClose(fd, err, cb);
  }
  /** Ends the stream and closes the file; cb once it is closed. */
  close(cb: ErrorCallback | null = null): void {
    if (cb !== null) {
      if (this.closed) {
        process.nextTick((): void => cb());
        return;
      }
      this.on("close", (...a: any[]): void => cb());
    }
    if (!this.writableEnded) this.end();
    else if (this.writableFinished) this.destroy();
  }
  /** The same as close() (Node: WriteStream.prototype.destroySoon). */
  destroySoon(cb: ErrorCallback | null = null): void {
    this.close(cb);
  }
}

function fsWriteOptions(options: FsStreamOptions | null): StreamOptions {
  const o: StreamOptions = { decodeStrings: true };
  if (options !== null) {
    const h = options.highWaterMark;
    if (h !== null && h !== undefined) o.highWaterMark = h;
    if (options.emitClose === false) o.emitClose = false;
    o.autoDestroy = options.autoClose !== false;
  }
  return o;
}

/** fs.createReadStream(path, options): the file as a Readable (64 KiB chunks; start and end are byte positions,
 * both included). The options can be an encoding. */
export function createReadStream(path: string, options: FsStreamOptions | string | null = null): ReadStream {
  if (typeof options === "string") return new ReadStream(path, { encoding: options as string });
  return new ReadStream(path, options as FsStreamOptions | null);
}
/** fs.createWriteStream(path, options): the file as a Writable (flags "w" by default: truncated; "a": appended). */
export function createWriteStream(path: string, options: FsStreamOptions | string | null = null): WriteStream {
  if (typeof options === "string") return new WriteStream(path, { encoding: options as string });
  return new WriteStream(path, options as FsStreamOptions | null);
}

export default Stream;
