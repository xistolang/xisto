import { isMainThread } from "node:worker_threads";
const NAME = "abc";
const codes = encode(NAME);
function encode(s: string): Uint8Array {
  return new TextEncoder().encode(s);
}
if (isMainThread) console.log(NAME, codes.length);
// top-level constants used only at the top level, in a program that imports worker_threads: they live in the
// program body each thread runs, and are released there (the exit path must not name them)
