// xisto:stream (node:stream): Readable, Writable, Duplex-style Transform and PassThrough, over bytes (Uint8Array).
// Chunks flow synchronously and in order: push() hands a chunk to the 'data' listeners (or to the piped
// destination) at once, so memory stays at one chunk however much flows through; end() and push(null) give 'end'
// (readable side) and 'finish' (writable side) once. Errors go to the 'error' listeners, or are thrown when there
// are none, as Node's unhandled 'error'.

/** What a transform calls when it is done with a chunk: an error, or nothing. */
export type TransformCallback = (error: Error | null) => void;

/** A source of bytes: listeners get each chunk ('data'), the end ('end') and a failure ('error'). */
export class Readable {
  private dataFns: ((chunk: Uint8Array) => void)[] = [];
  private endFns: (() => void)[] = [];
  private errorFns: ((err: Error) => void)[] = [];
  private closeFns: (() => void)[] = [];
  private targets: Writable[] = [];
  private buffered: Uint8Array[] = [];
  private ended: boolean = false;
  private endSent: boolean = false;
  readableEnded: boolean = false;
  destroyed: boolean = false;

  /** What a subclass does when readers want more (it may push()); chunks pushed by hand need nothing here. */
  _read(size: int): void {}

  /** Hands a chunk to the readers (null: the end). Returns true: there is no backpressure to wait for. */
  push(chunk: Uint8Array | null): boolean {
    if (chunk === null) {
      this.ended = true;
      this.flushEnd();
      return false;
    }
    if (this.ended) {
      this.emitError(new Error("stream.push() after EOF"));
      return false;
    }
    if (this.dataFns.length === 0 && this.targets.length === 0) {
      this.buffered.push(chunk);
      return true;
    }
    this.deliver(chunk);
    return true;
  }

  private deliver(chunk: Uint8Array): void {
    for (const fn of this.dataFns) fn(chunk);
    for (const t of this.targets) t.write(chunk);
  }

  private flushBuffered(): void {
    if (this.dataFns.length === 0 && this.targets.length === 0) return;
    const pending = this.buffered;
    this.buffered = [];
    for (const c of pending) this.deliver(c);
    this.flushEnd();
  }

  private flushEnd(): void {
    if (!this.ended || this.endSent || this.buffered.length > 0) return;
    if (this.dataFns.length === 0 && this.targets.length === 0 && this.endFns.length === 0) return;
    this.endSent = true;
    this.readableEnded = true;
    for (const fn of this.endFns) fn();
    for (const t of this.targets) t.end();
    for (const fn of this.closeFns) fn();
  }

  /** Listens for 'data' (each chunk). */
  on(event: string, fn: (chunk: Uint8Array) => void): Readable {
    if (event === "data") {
      this.dataFns.push(fn);
      this.flushBuffered();
    } else if (event === "end") {
      this.endFns.push((): void => fn(new Uint8Array(0)));
      this.flushEnd();
    } else if (event === "close") {
      this.closeFns.push((): void => fn(new Uint8Array(0)));
    }
    return this;
  }

  /** Listens for 'error'. */
  onError(fn: (err: Error) => void): Readable {
    this.errorFns.push(fn);
    return this;
  }

  /** on('data', (chunk: Buffer) => ...): each chunk as a Buffer, as Node gives it (a view on the chunk's bytes, not a copy). */
  __on_data__Buffer(fn: (chunk: Buffer) => void): Readable {
    return this.on("data", (c: Uint8Array): void => fn(Buffer.from(c.buffer, c.byteOffset, c.byteLength)));
  }

  /** on('error', fn): as onError. */
  __on_error(fn: (err: Error) => void): Readable {
    return this.onError(fn);
  }

  /** Sends every chunk to dest, then ends it; returns dest. */
  pipe<W extends Writable>(dest: W): W {
    this.targets.push(dest);
    this.flushBuffered();
    return dest;
  }

  /** Reports err to the 'error' listeners (thrown when there are none) and stops the stream. */
  destroy(err: Error | null): Readable {
    this.destroyed = true;
    if (err !== null) this.emitError(err);
    return this;
  }

  protected emitError(err: Error): void {
    if (this.errorFns.length === 0) throw err;
    for (const fn of this.errorFns) fn(err);
  }

  /** A readable that gives the chunks, in order, then ends. */
  static from(chunks: Uint8Array[]): Readable {
    const r = new Readable();
    for (const c of chunks) r.push(c);
    r.push(null);
    return r;
  }
}

/** A destination of bytes: write() each chunk, end() once; 'finish' when it has all of them. */
export class Writable {
  private finishFns: (() => void)[] = [];
  private errorFns: ((err: Error) => void)[] = [];
  private finished: boolean = false;
  writableEnded: boolean = false;
  writableFinished: boolean = false;

  /** Takes a chunk. Returns true: there is no backpressure to wait for. */
  write(chunk: Uint8Array): boolean {
    if (this.writableEnded) {
      this.reportError(new Error("write after end"));
      return false;
    }
    this._write(chunk, "buffer", (err: Error | null): void => {
      if (err !== null) this.reportError(err);
    });
    return true;
  }

  /** What a subclass does with each chunk, then callback(null) (or callback(error)). */
  _write(chunk: Uint8Array, encoding: string, callback: TransformCallback): void {
    callback(null);
  }

  /** What a subclass does at the end, before 'finish'. */
  _final(callback: TransformCallback): void {
    callback(null);
  }

  /** No more chunks: 'finish' after the last one. */
  end(): Writable {
    if (this.writableEnded) return this;
    this.writableEnded = true;
    this._final((err: Error | null): void => {
      if (err !== null) this.reportError(err);
    });
    this.finished = true;
    this.writableFinished = true;
    for (const fn of this.finishFns) fn();
    return this;
  }

  /** Listens for 'finish' (or 'close'). */
  onFinish(fn: () => void): Writable {
    if (this.finished) fn();
    else this.finishFns.push(fn);
    return this;
  }

  /** Listens for 'finish' or 'close' (the argument is an empty chunk). */
  on(event: string, fn: (chunk: Uint8Array) => void): Writable {
    if (event === "finish" || event === "close") this.onFinish((): void => fn(new Uint8Array(0)));
    return this;
  }

  /** Listens for 'error'. */
  onWriteError(fn: (err: Error) => void): Writable {
    this.errorFns.push(fn);
    return this;
  }

  /** on('error', fn): as onWriteError. */
  __on_error(fn: (err: Error) => void): Writable {
    return this.onWriteError(fn);
  }

  protected reportError(err: Error): void {
    if (this.errorFns.length === 0) throw err;
    for (const fn of this.errorFns) fn(err);
  }
}

/** Bytes in, bytes out: _transform(chunk, encoding, callback) pushes what comes out of each chunk, and the readable side
 * gives it to its readers. */
export class Transform extends Writable {
  readonly readable: Readable = new Readable();

  /** What a subclass does with each chunk: push() the output, then callback(null) (or callback(error)). */
  _transform(chunk: Uint8Array, encoding: string, callback: TransformCallback): void {
    this.push(chunk);
    callback(null);
  }

  /** What a subclass pushes at the end, before the readable side ends. */
  _flush(callback: TransformCallback): void {
    callback(null);
  }

  /** Output for the readable side (null: the end). */
  push(chunk: Uint8Array | null): boolean {
    return this.readable.push(chunk);
  }

  override _write(chunk: Uint8Array, encoding: string, callback: TransformCallback): void {
    this._transform(chunk, encoding, (err: Error | null): void => {
      if (err !== null) this.readable.destroy(err);
      callback(null);
    });
  }

  override _final(callback: TransformCallback): void {
    this._flush((err: Error | null): void => {
      if (err !== null) this.readable.destroy(err);
    });
    if (!this.readable.readableEnded) this.readable.push(null);
    callback(null);
  }

  /** Listens for 'finish' (the writable side), or 'data', 'end', 'close' (the readable side). */
  override on(event: string, fn: (chunk: Uint8Array) => void): Writable {
    if (event === "finish") this.onFinish((): void => fn(new Uint8Array(0)));
    else this.readable.on(event, fn);
    return this;
  }

  /** Listens for 'error' on the readable side. */
  onError(fn: (err: Error) => void): Transform {
    this.readable.onError(fn);
    return this;
  }

  /** on('data', (chunk: Buffer) => ...): the output, each chunk as a Buffer. */
  __on_data__Buffer(fn: (chunk: Buffer) => void): Writable {
    this.readable.__on_data__Buffer(fn);
    return this;
  }

  /** on('error', fn): a failure of either side (a transform's, or a write after end). */
  override __on_error(fn: (err: Error) => void): Writable {
    this.readable.onError(fn);
    return this.onWriteError(fn);
  }

  /** Sends the output to dest, then ends it; returns dest. */
  pipe<W extends Writable>(dest: W): W {
    return this.readable.pipe(dest);
  }
}

/** A Transform that gives every chunk through unchanged. */
export class PassThrough extends Transform {}
