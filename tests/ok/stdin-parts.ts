// readStdinParts (xisto:io): standard input cut before each ">" (tests/run.sh feeds stdin-parts.stdin), the parts
// in order and together the whole input; parts changed in place; the options checked at the first part.
import { readStdinParts } from "xisto:io";

const decoder = new TextDecoder();
let total = 0;
let n = 0;
for (const part of readStdinParts({ splitBefore: ">" })) {
  n++;
  total += part.length;
  console.log(`${n}: ${part.length} ${JSON.stringify(decoder.decode(part))}`);
  part.reverse(); // a view of its own bytes: the next parts do not change
}
console.log(`${n} parts, ${total} bytes`);

function attempt(parts: () => Generator<Uint8Array>): void {
  try {
    for (const part of parts()) console.log(part.length);
    console.log("no error");
  } catch (e) {
    console.log((e as Error).message);
  }
}
attempt(() => readStdinParts({}));
attempt(() => readStdinParts({ splitBefore: ">", splitAfter: "\n" }));
attempt(() => readStdinParts({ splitBefore: "ab" }));
attempt(() => readStdinParts({ splitAfter: "é" }));
