// node:stream's Stream as the module (Stream.Readable, Stream.Transform, Stream.pipeline...: its statics are the module's
// exports), extends/instanceof with a dotted name, and Writable.pipe (Node's legacy Stream.prototype.pipe)
import * as stream from "node:stream";
import { Stream, Writable, PassThrough } from "node:stream";

class Upper extends Stream.Transform {
  _transform(chunk: any, enc: string, cb: stream.TransformCallback): void {
    cb(null, String(chunk).toUpperCase());
  }
}
class Exclaim extends stream.Transform {
  _transform(chunk: any, enc: string, cb: stream.TransformCallback): void {
    cb(null, String(chunk) + "!");
  }
}

async function main(): Promise<void> {
  const p = new stream.Stream.PassThrough();
  const d = new Stream.Duplex({ read(): void {}, write(c: any, e: string, cb: (err?: Error | null) => void): void { cb(); } });
  console.log(p instanceof Stream.PassThrough, p instanceof stream.Stream.Transform, d instanceof Stream.Duplex, p instanceof Stream.Writable,
    typeof Stream.pipeline, typeof stream.Stream.finished);
  const out: string[] = [];
  const sink = new Stream.Writable({
    write(c: any, e: string, cb: (err?: Error | null) => void): void {
      out.push(String(c));
      cb();
    },
  });
  await Stream.promises.pipeline(Stream.Readable.from(["a", "b"]), new Upper(), new Exclaim(), sink);
  console.log(out.join(" "));
  Stream.pipeline(stream.Stream.Readable.from(["x"]), new Upper(), new stream.Stream.Writable({
    write(c: any, e: string, cb: (err?: Error | null) => void): void {
      console.log("piped", String(c));
      cb();
    },
  }), (err?: Error | null) => console.log("pipeline done", err ?? null));
  await new Promise<void>((r) => setTimeout(r, 5));

  // Writable.pipe: an 'error' (it is not readable)
  const w = new Writable({ write(c: any, e: string, cb: (err?: Error | null) => void): void { cb(); } });
  w.on("error", (e: Error) => console.log("w error", e.message, (e as any).code));
  w.pipe(new PassThrough());
  await new Promise<void>((r) => setTimeout(r, 5));

  // a plain Stream's pipe is the legacy one: its 'data' to dest, its 'end' ends dest
  const legacy = new Stream();
  const dest = new PassThrough();
  dest.on("pipe", (src: any) => console.log("pipe event", src === legacy));
  dest.on("data", (c: Buffer) => console.log("dest got", c.toString()));
  dest.on("finish", () => console.log("dest finished"));
  console.log(legacy.pipe(dest) === dest);
  legacy.emit("data", "hello");
  legacy.emit("end");
  await new Promise<void>((r) => setTimeout(r, 5));
}
main();
