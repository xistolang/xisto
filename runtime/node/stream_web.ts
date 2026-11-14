// xisto:stream/web (node:stream/web): the WHATWG streams: ReadableStream (default readers, pipeTo, pipeThrough, tee,
// for await), WritableStream (default writers), TransformStream, TextEncoderStream, TextDecoderStream and the
// queuing strategies. The algorithms are the spec's (queues with a highWaterMark and sizes, pull on demand,
// backpressure through desiredSize and writer.ready); the callbacks the spec runs "when a promise settles" run as
// microtasks (queueMicrotask). Differences (docs/docs/guides/node-compatibility.md):
//   - the source, sink and transformer are objects of functions written as properties (start: (c) => { ... });
//     their `this` is not the object; a function may return a promise (async) or nothing;
//   - no byte streams (type: "bytes", BYOB readers), no transfer, no ReadableStream.from;
//   - a microtask count between two steps can differ from Node's (the order of the steps of one stream does not).
// The fetch() Response's body keeps xisto's built-in ReadableStream (always null: fetch reads the body whole); a
// module that imports ReadableStream from here gets this one.

import { TypeError } from "xisto:errors";

/** What a read gives: a chunk, or done at the end. */
export class ReadableStreamReadResult {
  value: any;
  done: boolean;
  constructor(value: any, done: boolean) {
    this.value = value;
    this.done = done;
  }
}

/** A queuing strategy: the highWaterMark, and the size of a chunk (1 when there is no size function). */
export interface QueuingStrategy {
  highWaterMark?: number;
  size?: (chunk: any) => number;
}

/** Counts chunks: each one is 1. */
export class CountQueuingStrategy {
  highWaterMark: number;
  size: (chunk: any) => number = (chunk: any): number => 1;
  constructor(init: QueuingStrategy) {
    const h = init.highWaterMark;
    this.highWaterMark = h !== null && h !== undefined ? h : 1;
  }
}
/** Counts bytes: a chunk is its byteLength. */
export class ByteLengthQueuingStrategy {
  highWaterMark: number;
  size: (chunk: any) => number = (chunk: any): number => (chunk as Uint8Array).byteLength;
  constructor(init: QueuingStrategy) {
    const h = init.highWaterMark;
    this.highWaterMark = h !== null && h !== undefined ? h : 1;
  }
}

class Resolvers {
  resolve: (v: any) => void;
  reject: (e: Error) => void;
  constructor(resolve: (v: any) => void, reject: (e: Error) => void) {
    this.resolve = resolve;
    this.reject = reject;
  }
}

class WebTypeError extends TypeError {
  code: string;
  constructor(msg: string, code: string) {
    super(msg);
    this.code = code;
  }
}
function typeError(msg: string, code: string): Error {
  return new WebTypeError(msg, code);
}
function asError(reason: any): Error {
  if (reason instanceof Error) return reason as Error;
  return new Error(reason === null ? "undefined" : String(reason));
}

/** A promise settled later from the outside; handled (no unhandled rejection) unless someone reads it. */
class Deferred {
  promise: Promise<void>;
  private res: (() => void) | null = null;
  private rej: ((e: Error) => void) | null = null;
  settled: boolean = false;
  constructor() {
    this.promise = new Promise<void>((resolve, reject) => {
      this.res = (): void => resolve();
      this.rej = (e: Error): void => reject(e);
    });
    this.promise.catch((e: Error): void => {});
  }
  resolve(): void {
    if (this.settled) return;
    this.settled = true;
    const r = this.res;
    if (r !== null) r();
  }
  reject(e: Error): void {
    if (this.settled) return;
    this.settled = true;
    const r = this.rej;
    if (r !== null) r(e);
  }
}

function settledPromise(err: Error | null): Promise<void> {
  if (err !== null) {
    const p = Promise.reject(err) as Promise<void>;
    return p;
  }
  return Promise.resolve();
}

// ---- ReadableStream ----

/** The functions a ReadableStream calls: start once, pull when it wants more, cancel when the reader gives up. */
export interface UnderlyingSource {
  start?: (controller: ReadableStreamDefaultController) => void | Promise<void>;
  pull?: (controller: ReadableStreamDefaultController) => void | Promise<void>;
  cancel?: (reason: any) => void | Promise<void>;
  type?: string;
}

/** What a source uses to give chunks: enqueue, close, error. */
export class ReadableStreamDefaultController {
  private s: ReadableStream;
  constructor(s: ReadableStream) {
    this.s = s;
  }
  /** How much the queue wants before it is full (null once errored). */
  get desiredSize(): number | null {
    return this.s._desiredSize();
  }
  enqueue(chunk: any = null): void {
    this.s._enqueue(chunk);
  }
  close(): void {
    this.s._closeRequest();
  }
  error(e: any = null): void {
    this.s._error(asError(e));
  }
}

/** A source of chunks read through a reader (getReader), piped (pipeTo, pipeThrough) or iterated (for await). */
export class ReadableStream {
  private source: UnderlyingSource | null;
  private controller: ReadableStreamDefaultController;
  private queue: any[] = [];
  private queueSizes: number[] = [];
  private queueTotal: number = 0;
  private hwm: number;
  private sizeFn: ((chunk: any) => number) | null = null;
  private state: string = "readable"; // readable, closed, errored
  private storedError: Error | null = null;
  private closeRequested: boolean = false;
  private started: boolean = false;
  private pulling: boolean = false;
  private pullAgain: boolean = false;
  _reader: ReadableStreamDefaultReader | null = null;
  _disturbed: boolean = false;

  constructor(source: UnderlyingSource | null = null, strategy: QueuingStrategy | null = null) {
    this.source = source;
    this.controller = new ReadableStreamDefaultController(this);
    let h: number = 1;
    if (strategy !== null) {
      const sh = strategy.highWaterMark;
      if (sh !== null && sh !== undefined) h = sh;
      const sz = strategy.size;
      if (sz !== null && sz !== undefined) this.sizeFn = sz;
    }
    this.hwm = h;
    let started: Promise<void> | null = null;
    if (source !== null) {
      const st = source.start;
      if (st !== null && st !== undefined) {
        try {
          started = this._asPromise(st(this.controller));
        } catch (e) {
          throw e;
        }
      }
    }
    const onStart = (): void => {
      this.started = true;
      this._callPullIfNeeded();
    };
    if (started === null) queueMicrotask(onStart);
    else {
      started.then(onStart, (e: Error): void => this._error(e));
    }
  }
  private _asPromise(r: void | Promise<void>): Promise<void> {
    const v: any = r;
    if (v === null) return Promise.resolve();
    return r as Promise<void>;
  }

  /** Whether a reader holds the stream. */
  get locked(): boolean {
    return this._reader !== null;
  }
  /** A reader: the only one until releaseLock(). */
  getReader(options: any = null): ReadableStreamDefaultReader {
    if (this._reader !== null) throw typeError("Invalid state: ReadableStream is locked", "ERR_INVALID_STATE");
    return new ReadableStreamDefaultReader(this);
  }
  /** Gives up the stream: the queue is dropped, the source's cancel runs. */
  cancel(reason: any = null): Promise<void> {
    if (this._reader !== null) return settledPromise(typeError("Invalid state: ReadableStream is locked", "ERR_INVALID_STATE"));
    return this._cancel(reason);
  }

  _desiredSize(): number | null {
    if (this.state === "errored") return null;
    if (this.state === "closed") return 0;
    return this.hwm - this.queueTotal;
  }
  private _canEnqueue(): boolean {
    return !this.closeRequested && this.state === "readable";
  }
  _enqueue(chunk: any): void {
    if (!this._canEnqueue()) throw typeError("Invalid state: Controller is already closed", "ERR_INVALID_STATE");
    const r = this._reader;
    if (r !== null && r._requests.length > 0) {
      r._fulfill(chunk, false);
    } else {
      let size: number = 1;
      const f = this.sizeFn;
      if (f !== null) {
        try {
          size = f(chunk);
        } catch (e) {
          this._error(e as Error);
          throw e;
        }
      }
      this.queue.push(chunk);
      this.queueSizes.push(size);
      this.queueTotal += size;
    }
    this._callPullIfNeeded();
  }
  _closeRequest(): void {
    if (!this._canEnqueue()) throw typeError("Invalid state: Controller is already closed", "ERR_INVALID_STATE");
    this.closeRequested = true;
    if (this.queue.length === 0) this._close();
  }
  private _close(): void {
    if (this.state !== "readable") return;
    this.state = "closed";
    const r = this._reader;
    if (r !== null) {
      while (r._requests.length > 0) r._fulfill(null, true);
      r._closedD.resolve();
    }
  }
  _error(e: Error): void {
    if (this.state !== "readable") return;
    this.state = "errored";
    this.storedError = e;
    this.queue = [];
    this.queueSizes = [];
    this.queueTotal = 0;
    const r = this._reader;
    if (r !== null) {
      const reqs = r._requests;
      r._requests = [];
      for (const q of reqs) q.reject(e);
      r._closedD.reject(e);
    }
  }
  private _shouldPull(): boolean {
    if (!this._canEnqueue() || !this.started) return false;
    const r = this._reader;
    if (r !== null && r._requests.length > 0) return true;
    const d = this._desiredSize();
    return d !== null && d > 0;
  }
  _callPullIfNeeded(): void {
    if (!this._shouldPull()) return;
    if (this.pulling) {
      this.pullAgain = true;
      return;
    }
    this.pulling = true;
    const src = this.source;
    const pull = src !== null ? src.pull : null;
    if (pull === null || pull === undefined) {
      queueMicrotask((): void => {
        this.pulling = false;
      });
      return;
    }
    let p: Promise<void>;
    try {
      p = this._asPromise(pull(this.controller));
    } catch (e) {
      this._error(e as Error);
      return;
    }
    p.then((): void => {
      this.pulling = false;
      if (this.pullAgain) {
        this.pullAgain = false;
        this._callPullIfNeeded();
      }
    }, (e: Error): void => this._error(e));
  }
  /** A read: a queued chunk at once, else a request the next enqueue fulfills. */
  _read(r: ReadableStreamDefaultReader): Promise<ReadableStreamReadResult> {
    this._disturbed = true;
    if (this.state === "closed") return Promise.resolve(new ReadableStreamReadResult(null, true));
    if (this.state === "errored") {
      const e = this.storedError as Error;
      return Promise.reject(e) as Promise<ReadableStreamReadResult>;
    }
    if (this.queue.length > 0) {
      const chunk: any = this.queue[0];
      this.queue.splice(0, 1);
      this.queueTotal -= this.queueSizes[0];
      this.queueSizes.splice(0, 1);
      if (this.queueTotal < 0) this.queueTotal = 0;
      if (this.closeRequested && this.queue.length === 0) this._close();
      else this._callPullIfNeeded();
      return Promise.resolve(new ReadableStreamReadResult(chunk, false));
    }
    const p = new Promise<ReadableStreamReadResult>((resolve, reject) => {
      r._requests.push(new Resolvers((v: any): void => resolve(v as ReadableStreamReadResult), (e: Error): void => reject(e)));
    });
    this._callPullIfNeeded();
    return p;
  }
  _cancel(reason: any): Promise<void> {
    this._disturbed = true;
    if (this.state === "closed") return Promise.resolve();
    if (this.state === "errored") return settledPromise(this.storedError);
    this.queue = [];
    this.queueSizes = [];
    this.queueTotal = 0;
    this._close();
    const src = this.source;
    const c = src !== null ? src.cancel : null;
    if (c === null || c === undefined) return Promise.resolve();
    try {
      const p = this._asPromise(c(reason));
      return p.then((): void => {});
    } catch (e) {
      return settledPromise(e as Error);
    }
  }
  _isClosed(): boolean {
    return this.state === "closed";
  }
  _isErrored(): boolean {
    return this.state === "errored";
  }
  _storedError(): Error | null {
    return this.storedError;
  }

  /** Reads every chunk and writes it to dest; closes dest at the end (unless preventClose), aborts it on an error. */
  pipeTo(dest: WritableStream, options: StreamPipeOptions | null = null): Promise<void> {
    if (this.locked) return settledPromise(typeError("Invalid state: ReadableStream is locked", "ERR_INVALID_STATE"));
    if (dest.locked) return settledPromise(typeError("Invalid state: WritableStream is locked", "ERR_INVALID_STATE"));
    const preventClose = options !== null && options.preventClose === true;
    const preventAbort = options !== null && options.preventAbort === true;
    const preventCancel = options !== null && options.preventCancel === true;
    const reader = this.getReader();
    const writer = dest.getWriter();
    return pipeLoop(this, reader, writer, preventClose, preventAbort, preventCancel);
  }
  /** pipeTo(transform.writable) and gives transform.readable. */
  pipeThrough(transform: ReadableWritablePair, options: StreamPipeOptions | null = null): ReadableStream {
    const p = this.pipeTo(transform.writable, options);
    p.catch((e: Error): void => {});
    return transform.readable;
  }
  /** Two streams that each give every chunk. */
  tee(): ReadableStream[] {
    const reader = this.getReader();
    let c1: ReadableStreamDefaultController | null = null;
    let c2: ReadableStreamDefaultController | null = null;
    let reading = false;
    let canceled1 = false;
    let canceled2 = false;
    const pullBoth = (): void => {
      if (reading) return;
      reading = true;
      reader.read().then((r: ReadableStreamReadResult): void => {
        reading = false;
        const a = c1;
        const b = c2;
        if (r.done) {
          if (a !== null && !canceled1) a.close();
          if (b !== null && !canceled2) b.close();
          return;
        }
        if (a !== null && !canceled1) a.enqueue(r.value);
        if (b !== null && !canceled2) b.enqueue(r.value);
      }, (e: Error): void => {
        reading = false;
        const a = c1;
        const b = c2;
        if (a !== null) a.error(e);
        if (b !== null) b.error(e);
      });
    };
    const s1 = new ReadableStream({
      start: (c: ReadableStreamDefaultController): void => {
        c1 = c;
      },
      pull: (c: ReadableStreamDefaultController): void => pullBoth(),
      cancel: (reason: any): void => {
        canceled1 = true;
        if (canceled2) {
          reader.cancel(reason).catch((e: Error): void => {});
        }
      },
    });
    const s2 = new ReadableStream({
      start: (c: ReadableStreamDefaultController): void => {
        c2 = c;
      },
      pull: (c: ReadableStreamDefaultController): void => pullBoth(),
      cancel: (reason: any): void => {
        canceled2 = true;
        if (canceled1) {
          reader.cancel(reason).catch((e: Error): void => {});
        }
      },
    });
    return [s1, s2];
  }
  /** for await (const chunk of stream): the chunks, then the stream is released. */
  async *[Symbol.asyncIterator](): AsyncGenerator<any> {
    const reader = this.getReader();
    while (true) {
      const r = await reader.read();
      if (r.done) break;
      yield r.value;
    }
    reader.releaseLock();
  }
  /** The same as for await over the stream. */
  async *values(): AsyncGenerator<any> {
    const reader = this.getReader();
    while (true) {
      const r = await reader.read();
      if (r.done) break;
      yield r.value;
    }
    reader.releaseLock();
  }
}

/** pipeTo's and pipeThrough's options. */
export interface StreamPipeOptions {
  preventClose?: boolean;
  preventAbort?: boolean;
  preventCancel?: boolean;
}

/** What pipeThrough takes: a writable side to pipe into and a readable side to give back (a TransformStream). */
export interface ReadableWritablePair {
  readable: ReadableStream;
  writable: WritableStream;
}

async function pipeLoop(src: ReadableStream, reader: ReadableStreamDefaultReader, writer: WritableStreamDefaultWriter,
  preventClose: boolean, preventAbort: boolean, preventCancel: boolean): Promise<void> {
  try {
    while (true) {
      await writer.ready;
      const r = await reader.read();
      if (r.done) break;
      const wp = writer.write(r.value);
      wp.catch((e: Error): void => {});
    }
  } catch (e) {
    const err = e as Error;
    if (src._isErrored()) {
      if (!preventAbort) await writer.abort(err).catch((x: Error): void => {});
    } else if (!preventCancel) {
      await reader.cancel(err).catch((x: Error): void => {});
    }
    writer.releaseLock();
    reader.releaseLock();
    throw err;
  }
  if (!preventClose) await writer.close();
  writer.releaseLock();
  reader.releaseLock();
}

/** The reader of a ReadableStream: read() a chunk at a time. */
export class ReadableStreamDefaultReader {
  _stream: ReadableStream | null;
  _requests: Resolvers[] = [];
  _closedD: Deferred = new Deferred();
  constructor(stream: ReadableStream) {
    if (stream._reader !== null) throw typeError("Invalid state: ReadableStream is locked", "ERR_INVALID_STATE");
    this._stream = stream;
    stream._reader = this;
    if (stream._isClosed()) this._closedD.resolve();
    else if (stream._isErrored()) this._closedD.reject(stream._storedError() as Error);
  }
  /** Settles when the stream closes (rejects when it errors or the lock is released). */
  get closed(): Promise<void> {
    return this._closedD.promise;
  }
  _fulfill(value: any, done: boolean): void {
    const q = this._requests[0];
    this._requests.splice(0, 1);
    q.resolve(new ReadableStreamReadResult(value, done));
  }
  /** The next chunk ({ value, done: false }), or { value: undefined, done: true } at the end. */
  read(): Promise<ReadableStreamReadResult> {
    const s = this._stream;
    if (s === null) return Promise.reject(typeError("Invalid state: The reader is not attached to a stream", "ERR_INVALID_STATE")) as Promise<ReadableStreamReadResult>;
    return s._read(this);
  }
  /** Cancels the stream (see ReadableStream.cancel). */
  cancel(reason: any = null): Promise<void> {
    const s = this._stream;
    if (s === null) return settledPromise(typeError("Invalid state: The reader is not attached to a stream", "ERR_INVALID_STATE"));
    return s._cancel(reason);
  }
  /** Lets the stream go (pending reads are rejected). */
  releaseLock(): void {
    const s = this._stream;
    if (s === null) return;
    const e = typeError("Invalid state: Releasing reader", "ERR_INVALID_STATE");
    const reqs = this._requests;
    this._requests = [];
    for (const q of reqs) q.reject(e);
    if (!this._closedD.settled) this._closedD.reject(e);
    else {
      this._closedD = new Deferred();
      this._closedD.reject(e);
    }
    s._reader = null;
    this._stream = null;
  }
}

// ---- WritableStream ----

/** The functions a WritableStream calls: start once, write per chunk (one at a time), close at the end, abort. */
export interface UnderlyingSink {
  start?: (controller: WritableStreamDefaultController) => void | Promise<void>;
  write?: (chunk: any, controller: WritableStreamDefaultController) => void | Promise<void>;
  close?: () => void | Promise<void>;
  abort?: (reason: any) => void | Promise<void>;
  type?: string;
}

/** What a sink uses to fail the stream. */
export class WritableStreamDefaultController {
  private s: WritableStream;
  constructor(s: WritableStream) {
    this.s = s;
  }
  error(e: any = null): void {
    this.s._errorFromSink(asError(e));
  }
}

class WriteRequest {
  chunk: any;
  size: number;
  d: Deferred;
  constructor(chunk: any, size: number, d: Deferred) {
    this.chunk = chunk;
    this.size = size;
    this.d = d;
  }
}

/** A destination of chunks written through a writer (getWriter) or piped into. */
export class WritableStream {
  private sink: UnderlyingSink | null;
  private controller: WritableStreamDefaultController;
  private hwm: number;
  private sizeFn: ((chunk: any) => number) | null = null;
  private queue: WriteRequest[] = [];
  private queueTotal: number = 0;
  private state: string = "writable"; // writable, closed, errored
  private storedError: Error | null = null;
  private started: boolean = false;
  private writing: boolean = false;
  private closeD: Deferred | null = null;
  _writer: WritableStreamDefaultWriter | null = null;

  constructor(sink: UnderlyingSink | null = null, strategy: QueuingStrategy | null = null) {
    this.sink = sink;
    this.controller = new WritableStreamDefaultController(this);
    let h: number = 1;
    if (strategy !== null) {
      const sh = strategy.highWaterMark;
      if (sh !== null && sh !== undefined) h = sh;
      const sz = strategy.size;
      if (sz !== null && sz !== undefined) this.sizeFn = sz;
    }
    this.hwm = h;
    let started: Promise<void> | null = null;
    if (sink !== null) {
      const st = sink.start;
      if (st !== null && st !== undefined) started = this._asPromise(st(this.controller));
    }
    const onStart = (): void => {
      this.started = true;
      this._advance();
    };
    if (started === null) queueMicrotask(onStart);
    else started.then(onStart, (e: Error): void => this._errorFromSink(e));
  }
  private _asPromise(r: void | Promise<void>): Promise<void> {
    const v: any = r;
    if (v === null) return Promise.resolve();
    return r as Promise<void>;
  }
  get locked(): boolean {
    return this._writer !== null;
  }
  getWriter(): WritableStreamDefaultWriter {
    if (this._writer !== null) throw typeError("Invalid state: WritableStream is locked", "ERR_INVALID_STATE");
    return new WritableStreamDefaultWriter(this);
  }
  close(): Promise<void> {
    if (this._writer !== null) return settledPromise(typeError("Invalid state: WritableStream is locked", "ERR_INVALID_STATE"));
    return this._close();
  }
  abort(reason: any = null): Promise<void> {
    if (this._writer !== null) return settledPromise(typeError("Invalid state: WritableStream is locked", "ERR_INVALID_STATE"));
    return this._abort(reason);
  }

  _desiredSize(): number | null {
    if (this.state === "errored") return null;
    if (this.state === "closed") return 0;
    return this.hwm - this.queueTotal;
  }
  _backpressure(): boolean {
    const d = this._desiredSize();
    return d !== null && d <= 0;
  }
  _write(chunk: any): Promise<void> {
    if (this.state === "errored") return settledPromise(this.storedError);
    if (this.state === "closed" || this.closeD !== null) return settledPromise(typeError("Invalid state: WritableStream is closed", "ERR_INVALID_STATE"));
    let size: number = 1;
    const f = this.sizeFn;
    if (f !== null) size = f(chunk);
    const d = new Deferred();
    const wasBp = this._backpressure();
    this.queue.push(new WriteRequest(chunk, size, d));
    this.queueTotal += size;
    const w = this._writer;
    if (!wasBp && this._backpressure() && w !== null) w._readyD = new Deferred();
    this._advance();
    return d.promise.then((): void => {});
  }
  _close(): Promise<void> {
    if (this.state === "errored") return settledPromise(this.storedError);
    if (this.state === "closed" || this.closeD !== null) return settledPromise(typeError("Invalid state: WritableStream is closed", "ERR_INVALID_STATE"));
    const d = new Deferred();
    this.closeD = d;
    const w = this._writer;
    if (w !== null && this._backpressure()) w._readyD.resolve();
    this._advance();
    return d.promise.then((): void => {});
  }
  _abort(reason: any): Promise<void> {
    if (this.state !== "writable") return Promise.resolve();
    const e = asError(reason);
    this._fail(e);
    const s = this.sink;
    const a = s !== null ? s.abort : null;
    if (a === null || a === undefined) return Promise.resolve();
    return this._asPromise(a(reason));
  }
  _errorFromSink(e: Error): void {
    if (this.state !== "writable") return;
    this._fail(e);
  }
  private _fail(e: Error): void {
    this.state = "errored";
    this.storedError = e;
    const q = this.queue;
    this.queue = [];
    this.queueTotal = 0;
    for (const r of q) r.d.reject(e);
    const c = this.closeD;
    if (c !== null) c.reject(e);
    const w = this._writer;
    if (w !== null) {
      w._closedD.reject(e);
      if (w._readyD.settled) w._readyD = new Deferred();
      w._readyD.reject(e);
    }
  }
  /** Writes the next queued chunk (one at a time), or closes once the queue is empty. */
  private _advance(): void {
    if (!this.started || this.writing || this.state !== "writable") return;
    if (this.queue.length === 0) {
      const c = this.closeD;
      if (c === null) return;
      this.writing = true;
      const s = this.sink;
      const cl = s !== null ? s.close : null;
      const p: Promise<void> = cl === null || cl === undefined ? Promise.resolve() : this._asPromise(cl());
      p.then((): void => {
        this.writing = false;
        this.state = "closed";
        c.resolve();
        const w = this._writer;
        if (w !== null) w._closedD.resolve();
      }, (e: Error): void => {
        this.writing = false;
        this._fail(e);
      });
      return;
    }
    const req = this.queue[0];
    this.writing = true;
    const s = this.sink;
    const wf = s !== null ? s.write : null;
    let p: Promise<void>;
    try {
      p = wf === null || wf === undefined ? Promise.resolve() : this._asPromise(wf(req.chunk, this.controller));
    } catch (e) {
      p = settledPromise(e as Error);
    }
    p.then((): void => {
      this.writing = false;
      if (this.state !== "writable") return;
      this.queue.splice(0, 1);
      this.queueTotal -= req.size;
      if (this.queueTotal < 0) this.queueTotal = 0;
      req.d.resolve();
      const w = this._writer;
      if (w !== null && !this._backpressure() && this.closeD === null) w._readyD.resolve();
      this._advance();
    }, (e: Error): void => {
      this.writing = false;
      this._fail(e);
    });
  }
  _isClosed(): boolean {
    return this.state === "closed";
  }
  _isErrored(): boolean {
    return this.state === "errored";
  }
  _storedError(): Error | null {
    return this.storedError;
  }
}

/** The writer of a WritableStream: write() chunks (await ready for backpressure), close() at the end. */
export class WritableStreamDefaultWriter {
  _stream: WritableStream | null;
  _closedD: Deferred = new Deferred();
  _readyD: Deferred = new Deferred();
  constructor(stream: WritableStream) {
    if (stream._writer !== null) throw typeError("Invalid state: WritableStream is locked", "ERR_INVALID_STATE");
    this._stream = stream;
    stream._writer = this;
    if (stream._isErrored()) {
      const e = stream._storedError() as Error;
      this._closedD.reject(e);
      this._readyD.reject(e);
    } else {
      if (stream._isClosed()) this._closedD.resolve();
      if (!stream._backpressure()) this._readyD.resolve();
    }
  }
  /** Settles when the stream has closed (rejects on an error). */
  get closed(): Promise<void> {
    return this._closedD.promise;
  }
  /** Settles when the stream wants more (desiredSize > 0). */
  get ready(): Promise<void> {
    return this._readyD.promise;
  }
  /** How much the stream wants before its queue is full (null once errored). */
  get desiredSize(): number | null {
    const s = this._stream;
    return s !== null ? s._desiredSize() : null;
  }
  /** Queues a chunk; settles once the sink has written it. */
  write(chunk: any = null): Promise<void> {
    const s = this._stream;
    if (s === null) return settledPromise(typeError("Invalid state: The writer is not attached to a stream", "ERR_INVALID_STATE"));
    return s._write(chunk);
  }
  /** Closes the stream once the queued chunks are written. */
  close(): Promise<void> {
    const s = this._stream;
    if (s === null) return settledPromise(typeError("Invalid state: The writer is not attached to a stream", "ERR_INVALID_STATE"));
    return s._close();
  }
  /** Fails the stream (the queued chunks are dropped). */
  abort(reason: any = null): Promise<void> {
    const s = this._stream;
    if (s === null) return settledPromise(typeError("Invalid state: The writer is not attached to a stream", "ERR_INVALID_STATE"));
    return s._abort(reason);
  }
  /** Lets the stream go. */
  releaseLock(): void {
    const s = this._stream;
    if (s === null) return;
    s._writer = null;
    this._stream = null;
  }
}

// ---- TransformStream ----

/** The functions a TransformStream calls: start once, transform per chunk (enqueue the output), flush at the end. */
export interface Transformer {
  start?: (controller: TransformStreamDefaultController) => void | Promise<void>;
  transform?: (chunk: any, controller: TransformStreamDefaultController) => void | Promise<void>;
  flush?: (controller: TransformStreamDefaultController) => void | Promise<void>;
}

/** What a transformer uses to give its output: enqueue, error, terminate. */
export class TransformStreamDefaultController {
  private rc: ReadableStreamDefaultController | null = null;
  private ts: TransformStream;
  constructor(ts: TransformStream) {
    this.ts = ts;
  }
  _setReadable(rc: ReadableStreamDefaultController): void {
    this.rc = rc;
  }
  get desiredSize(): number | null {
    const rc = this.rc;
    return rc !== null ? rc.desiredSize : null;
  }
  enqueue(chunk: any = null): void {
    const rc = this.rc;
    if (rc !== null) rc.enqueue(chunk);
  }
  error(e: any = null): void {
    this.ts._error(asError(e));
  }
  terminate(): void {
    const rc = this.rc;
    if (rc !== null) rc.close();
  }
}

/** A writable side and a readable side: what is written goes through transform() into the readable side. */
export class TransformStream {
  readable: ReadableStream;
  writable: WritableStream;
  private tc: TransformStreamDefaultController;
  private rc: ReadableStreamDefaultController | null = null;
  private pullWaiter: (() => void) | null = null;

  constructor(transformer: Transformer | null = null, writableStrategy: QueuingStrategy | null = null, readableStrategy: QueuingStrategy | null = null) {
    this.tc = new TransformStreamDefaultController(this);
    const tr = transformer;
    const rs: QueuingStrategy = readableStrategy !== null ? readableStrategy : { highWaterMark: 0 };
    this.readable = new ReadableStream({
      start: (c: ReadableStreamDefaultController): void => {
        this.rc = c;
        this.tc._setReadable(c);
      },
      pull: (c: ReadableStreamDefaultController): void => {
        const w = this.pullWaiter;
        this.pullWaiter = null;
        if (w !== null) w();
      },
      cancel: (reason: any): void => {
        this._error(asError(reason));
      },
    }, rs);
    this.writable = new WritableStream({
      start: (c: WritableStreamDefaultController): void | Promise<void> => {
        if (tr !== null) {
          const st = tr.start;
          if (st !== null && st !== undefined) return st(this.tc);
        }
      },
      write: (chunk: any, c: WritableStreamDefaultController): void | Promise<void> => {
        return this._transformChunk(chunk);
      },
      close: (): void | Promise<void> => {
        return this._flushThenClose();
      },
      abort: (reason: any): void => {
        this._error(asError(reason));
      },
    }, writableStrategy);
    if (tr !== null) {
      const t = tr.transform;
      if (t !== null && t !== undefined) this.transformFn = t;
      const f = tr.flush;
      if (f !== null && f !== undefined) this.flushFn = f;
    }
  }
  private transformFn: ((chunk: any, controller: TransformStreamDefaultController) => void | Promise<void>) | null = null;
  private flushFn: ((controller: TransformStreamDefaultController) => void | Promise<void>) | null = null;

  private async _transformChunk(chunk: any): Promise<void> {
    const t = this.transformFn;
    if (t === null) this.tc.enqueue(chunk);
    else await t(chunk, this.tc);
    const rc = this.rc;
    const d = rc !== null ? rc.desiredSize : null;
    if (d !== null && d <= 0) {
      await new Promise<void>((resolve) => {
        this.pullWaiter = (): void => resolve();
      });
    }
  }
  private async _flushThenClose(): Promise<void> {
    const f = this.flushFn;
    if (f !== null) await f(this.tc);
    const rc = this.rc;
    if (rc !== null && rc.desiredSize !== null) {
      try {
        rc.close();
      } catch (e) {
        // (terminate() closed it already)
      }
    }
  }
  _error(e: Error): void {
    const rc = this.rc;
    if (rc !== null) rc.error(e);
    this.writable._errorFromSink(e);
    const w = this.pullWaiter;
    this.pullWaiter = null;
    if (w !== null) w();
  }
}

// ---- text streams ----

function encoderTransformer(): Transformer {
  const enc = new TextEncoder();
  return {
    transform: (chunk: any, c: TransformStreamDefaultController): void => {
      const s = String(chunk);
      if (s.length > 0) c.enqueue(enc.encode(s));
    },
  };
}
/** How many bytes at the end of b start a UTF-8 character that is not whole yet. */
function utf8Incomplete(b: Uint8Array): int {
  const n = b.length;
  for (let back = 1; back <= 3 && back <= n; back++) {
    const c = b[n - back];
    if ((c & 0xc0) === 0x80) continue;
    const need = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
    return need > back ? back : 0;
  }
  return 0;
}
class Utf8Pending {
  rest: Uint8Array = new Uint8Array(0);
}
function decoderTransformer(label: string): Transformer {
  const dec = new TextDecoder(label);
  const st = new Utf8Pending();
  return {
    transform: (chunk: any, c: TransformStreamDefaultController): void => {
      let b = chunk as Uint8Array;
      if (st.rest.length > 0) {
        const j = new Uint8Array(st.rest.length + b.length);
        j.set(st.rest, 0);
        j.set(b, st.rest.length);
        b = j;
      }
      const keep = utf8Incomplete(b);
      st.rest = keep > 0 ? b.slice(b.length - keep) : new Uint8Array(0);
      const s = dec.decode(keep > 0 ? b.subarray(0, b.length - keep) : b);
      if (s.length > 0) c.enqueue(s);
    },
    flush: (c: TransformStreamDefaultController): void => {
      if (st.rest.length === 0) return;
      const s = dec.decode(st.rest);
      st.rest = new Uint8Array(0);
      if (s.length > 0) c.enqueue(s);
    },
  };
}

/** Strings in, their UTF-8 bytes out. */
export class TextEncoderStream extends TransformStream {
  readonly encoding: string = "utf-8";
  constructor() {
    super(encoderTransformer());
  }
}

/** Bytes in, text out (a character cut between two chunks waits for the rest of it). */
export class TextDecoderStream extends TransformStream {
  readonly encoding: string = "utf-8";
  constructor(label: string = "utf-8") {
    super(decoderTransformer(label));
  }
}
