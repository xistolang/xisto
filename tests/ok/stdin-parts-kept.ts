// readStdinParts kept past the iteration (inplace.c leaves it alone): the parts pushed into an array and read
// after the loop keep their own bytes, as in Node.
import { readStdinParts } from "xisto:io";

function size(part: Uint8Array): int {
  return part.length;
}
const all: Uint8Array[] = [];
let total = 0;
for (const part of readStdinParts({ splitBefore: ">" })) {
  all.push(part);
  total += size(part);
}
const decoder = new TextDecoder();
for (const part of all) console.log(JSON.stringify(decoder.decode(part)));
console.log(`total ${total}`);
