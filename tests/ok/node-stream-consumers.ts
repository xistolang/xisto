// node:stream/consumers: text, json, buffer, bytes, arrayBuffer of a readable stream
import { Readable, PassThrough } from "node:stream";
import { text, json, buffer, bytes, arrayBuffer } from "node:stream/consumers";
import * as consumers from "node:stream/consumers";

async function main(): Promise<void> {
  console.log(await text(Readable.from(["héllo", " ", "wörld"])));
  const pt = new PassThrough();
  pt.write(Buffer.from([0xe2, 0x82]));
  pt.end(Buffer.from([0xac, 0x21]));
  console.log(await consumers.text(pt));
  const j = await json(Readable.from(['{"a": [1, ', '2], "b": "x"}']));
  console.log(j, j.a[1], j.b);
  const b = await buffer(Readable.from([Buffer.from("ab"), Buffer.from("cd")]));
  console.log(b, b.length, b.toString("hex"));
  const u = await bytes(Readable.from(["xyz"]));
  console.log(u, u.length);
  const ab = await arrayBuffer(Readable.from(["12345"]));
  console.log(ab.byteLength, new Uint8Array(ab)[0]);
  const empty = await text(Readable.from([]));
  console.log(JSON.stringify(empty));
  try {
    await json(Readable.from(["{bad"]));
  } catch (e) {
    console.log((e as Error).name);
  }
  const failing = new Readable({ read(): void {} });
  setTimeout(() => failing.destroy(new Error("boom")), 1);
  try {
    await text(failing);
  } catch (e) {
    console.log("rejected:", (e as Error).message);
  }
}
main();
