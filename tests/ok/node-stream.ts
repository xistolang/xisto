// node:stream: Readable.from and pushed chunks, pipe into a Writable, a Transform subclass with _transform and
// _flush, PassThrough, and many chunks flowing through a chain (one chunk at a time, nothing kept).
import { PassThrough, Readable, Transform, Writable } from "node:stream";
import type { TransformCallback } from "node:stream";

const enc = new TextEncoder();
const dec = new TextDecoder();

class Collect extends Writable {
  parts: string[] = [];
  bytes: int = 0;
  override _write(chunk: Uint8Array, encoding: string, callback: TransformCallback): void {
    this.parts.push(dec.decode(chunk));
    this.bytes += chunk.length;
    callback(null);
  }
}

class Upper extends Transform {
  override _transform(chunk: Uint8Array, encoding: string, callback: TransformCallback): void {
    this.push(enc.encode(dec.decode(chunk).toUpperCase()));
    callback(null);
  }
  override _flush(callback: TransformCallback): void {
    this.push(enc.encode("!"));
    callback(null);
  }
}

class Source extends Readable {
  override _read(size: int): void {}
}

function step1(next: () => void): void {
  const sink = new Collect();
  sink.on("finish", (): void => {
    console.log("from + pipe:", sink.parts.join("|"));
    next();
  });
  Readable.from([enc.encode("alpha"), enc.encode("beta"), enc.encode("gamma")]).pipe(sink);
}

function step2(next: () => void): void {
  const src = new Source();
  const got: string[] = [];
  src.on("data", (chunk: Uint8Array): void => {
    got.push(dec.decode(chunk));
  });
  src.on("end", (): void => {
    console.log("push + data/end:", got.join(","));
    next();
  });
  src.push(enc.encode("one"));
  src.push(enc.encode("two"));
  src.push(null);
}

function step3(next: () => void): void {
  const up = new Upper();
  const sink = new Collect();
  sink.on("finish", (): void => {
    console.log("transform:", sink.parts.join(""));
    next();
  });
  Readable.from([enc.encode("hello "), enc.encode("stream")]).pipe(up).pipe(sink);
}

function step4(next: () => void): void {
  const pass = new PassThrough();
  const parts: string[] = [];
  pass.on("data", (chunk: Uint8Array): void => {
    parts.push(dec.decode(chunk));
  });
  pass.on("end", (): void => {
    console.log("passthrough:", parts.join("+"));
    next();
  });
  pass.write(enc.encode("x"));
  pass.write(enc.encode("y"));
  pass.end();
}

function step5(next: () => void): void {
  // 2000 chunks of 4 KiB through PassThrough -> Upper -> Collect
  const pass = new PassThrough();
  const up = new Upper();
  const sink = new Collect();
  sink.on("finish", (): void => {
    console.log("chain bytes:", sink.bytes, "chunks seen:", sink.parts.length > 0);
    next();
  });
  pass.pipe(up).pipe(sink);
  const block = enc.encode("abcd".repeat(1024));
  for (let i = 0; i < 2000; i++) pass.write(block);
  pass.end();
}

step1((): void => step2((): void => step3((): void => step4((): void => step5((): void => console.log("done"))))));
