// node:stream pipeline() and finished(), node:stream/promises, Readable.from over an array, a generator and an
// async generator, and for await over a Readable.
import { PassThrough, Readable, Transform, Writable, finished, pipeline } from "node:stream";
import { pipeline as pipelineP, finished as finishedP } from "node:stream/promises";
import type { TransformCallback } from "node:stream";

function log(...a: any[]): void {
  console.log(a.join(" "));
}

function sink(name: string, out: string[]): Writable {
  return new Writable({
    objectMode: true,
    write: (chunk: any, encoding: string, cb: (error?: Error | null) => void): void => {
      out.push(String(chunk));
      cb();
    },
  });
}

function upper(): Transform {
  return new Transform({
    objectMode: true,
    transform: (chunk: any, encoding: string, cb: TransformCallback): void => {
      cb(null, String(chunk).toUpperCase());
    },
  });
}

function* gen(): Generator<string> {
  yield "g1";
  yield "g2";
  yield "g3";
}

async function* agen(): AsyncGenerator<string> {
  yield "a1";
  await new Promise<void>((r) => setTimeout(r, 2));
  yield "a2";
}

function step1(next: () => void): void {
  const out: string[] = [];
  pipeline(Readable.from(["x", "y", "z"]), upper(), sink("s1", out), (err?: Error | null) => {
    log("pipeline1 done", err === null || err === undefined ? "ok" : err.message, out.join(","));
    next();
  });
}

function step2(next: () => void): void {
  const out: string[] = [];
  const bad = new Transform({
    objectMode: true,
    transform: (chunk: any, encoding: string, cb: TransformCallback): void => {
      if (chunk === "y") cb(new Error("no y"));
      else cb(null, chunk);
    },
  });
  const src = Readable.from(["x", "y", "z"]);
  const dst = sink("s2", out);
  src.on("close", () => log("src close"));
  dst.on("close", () => log("dst close"));
  pipeline(src, bad, dst, (err?: Error | null) => {
    log("pipeline2 done", err === null || err === undefined ? "ok" : err.message, out.join(","), src.destroyed, dst.destroyed);
    next();
  });
}

function step3(next: () => void): void {
  const p = new PassThrough();
  finished(p, (err?: Error | null) => {
    log("finished p", err === null || err === undefined ? "ok" : err.message);
    next();
  });
  p.resume();
  p.end("bye");
}

function step4(next: () => void): void {
  const r = new Readable({ read: (size: number): void => {} });
  finished(r, (err?: Error | null) => {
    log("finished r", err === null || err === undefined ? "ok" : (err as any).code);
    next();
  });
  r.destroy();
}

async function step5(): Promise<void> {
  const out: string[] = [];
  await pipelineP(Readable.from(gen()), upper(), sink("s5", out));
  log("promise pipeline", out.join(","));
  const out2: string[] = [];
  await pipelineP(Readable.from(agen()), upper(), sink("s6", out2));
  log("promise pipeline async", out2.join(","));
  const w = sink("s7", []);
  w.end("x");
  await finishedP(w);
  log("finishedP ok");
  const chunks: string[] = [];
  for await (const c of Readable.from(["p", "q"])) chunks.push(c as string);
  log("for await", chunks.join(","));
  const r = new Readable({ read: (size: number): void => {} });
  r.push("abc");
  r.push("def");
  r.push(null);
  for await (const c of r) log("chunk", (c as Buffer).toString());
  try {
    await pipelineP(Readable.from(["a"]), new Transform({
      transform: (chunk: any, encoding: string, cb: TransformCallback): void => cb(new Error("tf failed")),
      objectMode: true,
    }), sink("s8", []));
  } catch (e) {
    log("caught", (e as Error).message);
  }
}

step1(() => step2(() => step3(() => step4(() => {
  const p: Promise<void> = step5();
}))));
