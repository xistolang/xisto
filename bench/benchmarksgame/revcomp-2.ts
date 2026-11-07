/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

reverse-complement, xisto #2: the program written for xisto rather than translated from JS. All of stdin is
read at once into bytes; each sequence's complement is written backwards, through a 256-byte table, into an
output buffer that gets a newline every 60 symbols.
 */

import { readFileSync } from "node:fs";

const PAIRS = "ATCGGCTAUAMKRYWWSSYRKMVBHDDHBVNN";
const table = new Uint8Array(256);
for (let i = 0; i < 256; i++) table[i] = i;
const pairs = new TextEncoder().encode(PAIRS);
for (let i = 0; i < pairs.length; i += 2) {
  table[pairs[i]] = pairs[i + 1];
  table[pairs[i] + 32] = pairs[i + 1]; // the lower case too
}

const buf = readFileSync(0);
const n = buf.length;
let i: int = 0;
while (i < n) {
  const h = buf.indexOf(10, i);
  const start = h < 0 ? n : h + 1; // after the header line
  const nx = buf.indexOf(62, start);
  const end = nx < 0 ? n : nx;
  // a: from the front, b: from the back (end - 1 is the sequence's last newline); each skips the newlines
  let a: int = start;
  let b: int = end - 2;
  let ca: int = 0; // symbols since the last newline before a
  const total = end - start - Math.trunc((end - start + 60) / 61); // symbols (without the newlines)
  let cb: int = total % 60 === 0 ? 59 : (total % 60) - 1; // b's position within its line
  let left: int = Math.trunc(total / 2);
  while (left > 0) {
    // a run where neither cursor reaches the end of its line: no newline to skip inside it
    let run: int = 60 - ca;
    if (cb + 1 < run) run = cb + 1;
    if (left < run) run = left;
    for (let t = 0; t < run; t++) {
      const x = buf[a + t];
      buf[a + t] = table[buf[b - t]];
      buf[b - t] = table[x];
    }
    a += run;
    b -= run;
    ca += run;
    cb -= run;
    left -= run;
    if (ca === 60) { a++; ca = 0; }
    if (cb < 0) { b--; cb = 59; } // over the newline, to the last symbol of the line before
  }
  if (total % 2 === 1) buf[a] = table[buf[a]];
  i = end;
}
process.stdout.write(buf);
