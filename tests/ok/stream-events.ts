// node:stream's on() per event, as Node's typings overload it: on('data', (b: Buffer) => ...) gets each chunk as a
// Buffer (a Uint8Array listener keeps getting the chunk), on('error', fn) the stream's failure, and on('end', resolve)
// settles a promise.
import { PassThrough, Transform } from "node:stream";
import type { TransformCallback } from "node:stream";

async function collect(): Promise<Buffer> {
  const out = new PassThrough();
  const parts: Buffer[] = [];
  out.on("data", (b: Buffer) => parts.push(b));
  const done = new Promise<void>((resolve, reject) => {
    out.on("end", resolve);
    out.on("error", reject);
  });
  out.write(new Uint8Array([104, 105]));
  out.write(Buffer.from(" there"));
  out.end();
  await done;
  console.log(parts.length, parts[0], Buffer.isBuffer(parts[1]));
  return Buffer.concat(parts);
}

class Rejects extends Transform {
  override _transform(chunk: Uint8Array, encoding: string, callback: TransformCallback): void {
    callback(new Error(`bad chunk of ${chunk.length}`));
  }
}

async function main(): Promise<void> {
  const all = await collect();
  console.log(all.toString());
  const p = new PassThrough();
  const seen: int[] = [];
  p.on("data", (u: Uint8Array) => seen.push(u.length));
  p.write(Buffer.from("abc"));
  p.end();
  await new Promise<void>((resolve) => setTimeout(resolve, 10));
  console.log("u8", seen.join(","));
  const r = new Rejects();
  const failed = new Promise<string>((resolve) => {
    r.on("error", (e: Error) => resolve(e.message));
  });
  r.write(Buffer.from("xy"));
  console.log("error:", await failed);
}
main();
