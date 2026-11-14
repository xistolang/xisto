// node:stream Readable: paused mode with read(n) and 'readable', object mode, setEncoding across a split
// character, _read on demand with a highWaterMark, pause/resume events.
import { Readable } from "node:stream";

function log(...a: any[]): void {
  console.log(a.join(" "));
}

function step1(next: () => void): void {
  const r = new Readable({ read: (size: number): void => {}, highWaterMark: 4 });
  log("push a", r.push("ab"));
  log("push b", r.push("cd"));
  log("push c", r.push("ef"));
  log("len", r.readableLength, "flowing", r.readableFlowing);
  r.on("readable", () => {
    let chunk: any;
    while ((chunk = r.read(3)) !== null) log("read3", (chunk as Buffer).toString());
    log("after loop len", r.readableLength);
  });
  r.on("end", () => {
    log("end");
    next();
  });
  setTimeout(() => {
    r.push(null);
  }, 5);
}

function step2(next: () => void): void {
  const r = new Readable({ objectMode: true, read: (size: number): void => {} });
  r.push({ a: 1 });
  r.push(42);
  r.push("str");
  r.push(null);
  r.on("data", (d: any) => log("obj", JSON.stringify(d)));
  r.on("end", () => {
    log("obj end");
    next();
  });
}

function step3(next: () => void): void {
  const r = new Readable({ read: (size: number): void => {} });
  r.setEncoding("utf8");
  const euro = Buffer.from("€uro");
  r.push(euro.subarray(0, 1));
  r.push(euro.subarray(1, 2));
  r.push(euro.subarray(2));
  r.push(null);
  r.on("data", (s: string) => log("str", JSON.stringify(s), typeof s));
  r.on("end", () => {
    log("enc end", r.readableEncoding);
    next();
  });
}

function step4(next: () => void): void {
  let i = 0;
  const r = new Readable({
    highWaterMark: 2,
    read: (size: number): void => {
      i++;
      log("_read", size, i);
      if (i > 3) r.push(null);
      else r.push(String(i));
    },
  });
  r.on("data", (c: Buffer) => log("data", c.toString()));
  r.on("end", () => log("end4"));
  r.on("close", () => {
    log("close4");
    next();
  });
  log("flowing?", r.readableFlowing);
}

function step5(next: () => void): void {
  const r = new Readable({ read: (size: number): void => {} });
  r.on("pause", () => log("event pause"));
  r.on("resume", () => log("event resume"));
  r.on("data", (c: Buffer) => {
    log("data5", c.toString());
    if (c.toString() === "x") {
      r.pause();
      log("paused", r.isPaused());
      setTimeout(() => {
        log("resuming");
        r.resume();
      }, 2);
    }
  });
  r.on("end", () => {
    log("end5");
    next();
  });
  r.push("x");
  r.push("y");
  r.push(null);
}

step1(() => step2(() => step3(() => step4(() => step5(() => log("all done"))))));
