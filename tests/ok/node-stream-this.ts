// node:stream option functions written as function (this: Readable, ...): this is the stream; for await with break.
import { Readable, Writable, Transform } from "node:stream";
import type { TransformCallback } from "node:stream";

let i = 0;
const r = new Readable({
  objectMode: true,
  read: function (this: Readable, size: number): void {
    i++;
    this.push(i <= 3 ? { n: i } : null);
  },
});
const t = new Transform({
  objectMode: true,
  transform: function (this: Transform, chunk: any, enc: string, cb: TransformCallback): void {
    this.push(chunk.n * 10);
    cb();
  },
});
r.pipe(t).on("data", (v: any) => console.log("v", v));

async function main(): Promise<void> {
  await new Promise<void>((res) => setTimeout(res, 5));
  let seen = 0;
  for await (const c of Readable.from(["a", "b", "c", "d"])) {
    seen++;
    if (c === "b") break;
  }
  console.log("seen", seen);
}
main();
