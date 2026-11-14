// node:stream pipe(): backpressure through a Transform into a slow Writable, two destinations and unpipe, a Duplex,
// destroy(err) ordering, a failing transform.
import { Duplex, PassThrough, Readable, Transform, Writable } from "node:stream";
import type { TransformCallback } from "node:stream";

function log(...a: any[]): void {
  console.log(a.join(" "));
}

class Upper extends Transform {
  override _transform(chunk: Buffer, encoding: string, callback: TransformCallback): void {
    callback(null, chunk.toString().toUpperCase());
  }
  override _flush(callback: TransformCallback): void {
    log("flush");
    this.push("!");
    callback();
  }
}

function step1(next: () => void): void {
  let n = 0;
  const src = new Readable({
    highWaterMark: 8,
    read: (size: number): void => {
      n++;
      if (n <= 6) src.push("chunk" + n + ";");
      else src.push(null);
    },
  });
  const seen: string[] = [];
  const slow: Writable = new Writable({
    highWaterMark: 10,
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => {
      seen.push((chunk as Buffer).toString());
      log("write", (chunk as Buffer).toString(), "len", slow.writableLength);
      setTimeout(() => cb(), 1);
    },
  });
  slow.on("drain", () => log("drain"));
  slow.on("pipe", (s: Readable) => log("pipe event", s === src));
  slow.on("finish", () => {
    log("finish", seen.length);
    next();
  });
  src.pipe(new Upper()).pipe(slow);
}

function step2(next: () => void): void {
  const src = new PassThrough();
  const a = new PassThrough();
  const b = new PassThrough();
  a.on("data", (c: Buffer) => log("a got", c.toString()));
  b.on("data", (c: Buffer) => log("b got", c.toString()));
  a.on("unpipe", (s: Readable) => log("a unpiped", s === src));
  src.pipe(a);
  src.pipe(b);
  src.write("one");
  setTimeout(() => {
    src.unpipe(a);
    src.write("two");
    src.end();
    b.on("end", () => {
      log("b end, a ended?", a.writableEnded);
      next();
    });
  }, 2);
}

class Echo extends Duplex {
  override _write(chunk: any, encoding: string, callback: (error?: Error | null) => void): void {
    log("duplex write", (chunk as Buffer).toString());
    this.push("echo:" + (chunk as Buffer).toString());
    callback();
  }
  override _final(callback: (error?: Error | null) => void): void {
    log("duplex final");
    this.push(null);
    callback();
  }
  override _read(size: number): void {}
}

function step3(next: () => void): void {
  const d = new Echo();
  d.on("data", (c: Buffer) => log("duplex data", c.toString()));
  d.on("end", () => log("duplex end"));
  d.on("finish", () => log("duplex finish"));
  d.on("close", () => {
    log("duplex close");
    next();
  });
  d.write("x");
  d.end("y");
}

function step4(next: () => void): void {
  const r = new Readable({ read: (size: number): void => {} });
  r.on("error", (e: Error) => log("r error", e.message));
  r.on("close", () => {
    log("r close", r.destroyed, r.readableAborted);
    next();
  });
  r.destroy(new Error("gone"));
  log("destroyed sync", r.destroyed);
  r.push("ignored");
}

function step5(next: () => void): void {
  const t = new Transform({
    transform: (chunk: any, encoding: string, cb: TransformCallback): void => {
      if ((chunk as Buffer).toString() === "bad") cb(new Error("bad chunk"));
      else cb(null, chunk);
    },
  });
  t.on("data", (c: Buffer) => log("t data", c.toString()));
  t.on("error", (e: Error) => log("t error", e.message));
  t.on("close", () => {
    log("t close");
    next();
  });
  t.write("good");
  t.write("bad");
  t.write("after");
}

step1(() => step2(() => step3(() => step4(() => step5(() => log("all done"))))));
