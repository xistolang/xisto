// node:stream event timing against process.nextTick, promises and timers; _construct, _destroy, unshift,
// allowHalfOpen: false, autoDestroy/emitClose off, object-mode backpressure; option functions with this; for await
// with break.
import { Duplex, PassThrough, Readable, Writable } from "node:stream";

function log(...a: any[]): void {
  console.log(a.join(" "));
}

class Slow extends Writable {
  override _construct(callback: (error?: Error | null) => void): void {
    log("construct start");
    setTimeout(() => {
      log("construct done");
      callback();
    }, 2);
  }
  override _write(chunk: any, encoding: string, callback: (error?: Error | null) => void): void {
    log("slow write", (chunk as Buffer).toString());
    callback();
  }
  override _destroy(err: Error | null, callback: (error?: Error | null) => void): void {
    log("slow destroy", err === null ? "null" : err.message);
    callback(err);
  }
}

function step1(next: () => void): void {
  const p = new PassThrough();
  p.on("data", (c: Buffer) => log("data", c.toString()));
  p.on("end", () => log("end"));
  p.on("finish", () => log("finish"));
  p.on("close", () => {
    log("close");
    next();
  });
  Promise.resolve().then(() => log("microtask 1"));
  process.nextTick(() => log("tick 1"));
  p.write("a", () => log("cb a"));
  process.nextTick(() => log("tick 2"));
  p.end("b", () => log("cb end"));
  Promise.resolve().then(() => log("microtask 2"));
  setTimeout(() => log("timer 0"), 0);
  log("sync end");
}

function step2(next: () => void): void {
  const s = new Slow();
  s.write("early");
  log("wrote early");
  s.end("last");
  s.on("finish", () => log("slow finish"));
  s.on("close", () => {
    log("slow close");
    next();
  });
}

function step3(next: () => void): void {
  const r = new Readable({ read: (size: number): void => {} });
  r.push("abc");
  r.once("readable", () => {
    log("readable once", r.read(1).toString(), r.readableLength);
    r.unshift("Z");
    log("after unshift", r.read().toString());
    r.push(null);
    r.on("end", () => {
      log("end3");
      next();
    });
    r.resume();
  });
}

function step4(next: () => void): void {
  const d = new Duplex({
    allowHalfOpen: false,
    read: (size: number): void => {},
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => cb(),
  });
  d.on("finish", () => log("d finish"));
  d.on("end", () => log("d end"));
  d.on("close", () => {
    log("d close");
    next();
  });
  d.resume();
  d.push(null);
}

function step5(next: () => void): void {
  const w = new Writable({
    autoDestroy: false,
    emitClose: false,
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => {
      setTimeout(() => cb(), 1);
    },
  });
  w.write("1", (e?: Error | null) => log("cb1", e === null || e === undefined ? "ok" : (e as any).code));
  w.write("2", (e?: Error | null) => log("cb2", e === null || e === undefined ? "ok" : (e as any).code));
  w.on("finish", () => log("finish5"));
  w.on("close", () => log("close5 (not expected)"));
  w.destroy(null, (err?: Error | null) => log("destroy cb", err === null || err === undefined ? "null" : err.message));
  setTimeout(() => {
    log("destroyed", w.destroyed, w.closed);
    next();
  }, 10);
}

function step6(next: () => void): void {
  const w = new Writable({
    objectMode: true,
    highWaterMark: 2,
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => {
      process.nextTick(() => cb());
    },
  });
  log("o1", w.write({}), "o2", w.write({}), "o3", w.write({}));
  w.on("drain", () => {
    log("drain6", w.writableLength);
    next();
  });
}

step1(() => step2(() => step3(() => step4(() => step5(() => step6(() => log("all done")))))));
