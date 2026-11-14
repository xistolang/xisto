// node:stream Writable: write() false above the highWaterMark and 'drain', end(chunk, cb), _final, cork/uncork
// into _writev, a failing _write, write after end, object mode; the callbacks and events on Node's ticks.
import { Writable } from "node:stream";

function log(...a: any[]): void {
  console.log(a.join(" "));
}

function step1(next: () => void): void {
  const pending: ((e?: Error | null) => void)[] = [];
  const w = new Writable({
    highWaterMark: 5,
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => {
      log("_write", (chunk as Buffer).toString(), encoding);
      pending.push(cb);
    },
  });
  log("w1", w.write("abc"));
  log("w2", w.write("def"));
  log("w3", w.write("g", (err?: Error | null) => log("cb g", err)));
  log("len", w.writableLength, "needDrain", w.writableNeedDrain);
  w.on("drain", () => log("drain"));
  w.on("finish", () => log("finish"));
  w.on("close", () => {
    log("close");
    next();
  });
  const flush = (): void => {
    if (pending.length > 0) {
      const cb = pending[0];
      pending.splice(0, 1);
      cb();
      setTimeout(flush, 1);
    } else {
      w.end("z", () => log("end cb"));
      if (pending.length > 0) {
        const c2 = pending[0];
        pending.splice(0, 1);
        c2();
      }
      log("ended", w.writableEnded, w.writableFinished);
    }
  };
  setTimeout(flush, 1);
}

function step2(next: () => void): void {
  const w = new Writable({
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => {
      log("sync write", (chunk as Buffer).toString());
      cb();
    },
    final: (cb: (error?: Error | null) => void): void => {
      log("final");
      setTimeout(() => cb(), 1);
    },
  });
  w.on("prefinish", () => log("prefinish"));
  w.on("finish", () => log("finish2"));
  w.on("close", () => {
    log("close2");
    next();
  });
  w.write("a", () => log("cb a"));
  w.write("b", () => log("cb b"));
  w.end(() => log("end cb2"));
  log("after end");
}

function step3(next: () => void): void {
  const w = new Writable({
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => {
      log("single", (chunk as Buffer).toString());
      setTimeout(() => cb(), 1);
    },
    writev: (chunks: Array<{ chunk: any; encoding: BufferEncoding }>, cb: (error?: Error | null) => void): void => {
      const parts: string[] = [];
      for (const c of chunks) parts.push((c.chunk as Buffer).toString());
      log("writev", parts.join(","));
      setTimeout(() => cb(), 1);
    },
  });
  w.cork();
  w.write("1");
  w.write("2");
  w.write("3");
  log("corked", w.writableCorked, w.writableLength);
  process.nextTick(() => {
    w.uncork();
    w.write("4");
    w.write("5");
    w.end();
  });
  w.on("finish", () => {
    log("finish3");
    next();
  });
}

function step4(next: () => void): void {
  const w = new Writable({
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => {
      cb(new Error("boom"));
    },
  });
  w.on("error", (e: Error) => log("error", e.message));
  w.on("close", () => {
    log("close4", w.destroyed, w.writableAborted);
    next();
  });
  w.write("x", (err?: Error | null) => log("write cb err", err !== null && err !== undefined ? err.message : "none"));
  log("sync after write");
}

function step5(next: () => void): void {
  const w = new Writable({
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => cb(),
  });
  w.on("error", (e: any) => log("error5", e.code, e.message));
  w.end();
  w.write("late", (err?: Error | null) => log("late cb", (err as any).code));
  w.on("close", () => {
    log("close5");
    next();
  });
}

function step6(next: () => void): void {
  const w = new Writable({
    objectMode: true,
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => {
      log("obj", JSON.stringify(chunk), encoding);
      cb();
    },
  });
  w.write({ k: 1 });
  w.write([1, 2]);
  w.end(7);
  w.on("finish", () => {
    log("finish6");
    next();
  });
}

step1(() => step2(() => step3(() => step4(() => step5(() => step6(() => log("all done")))))));
