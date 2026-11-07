/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

reverse-complement, xisto #3: as #2 (all of stdin read at once, each sequence written backwards through a
256-byte table, a newline every 60 symbols), but into a second buffer, line by line: the symbols of an output
line are a run of the input read backwards, or two runs when the line straddles an input newline, and each
run is a loop the compiler turns into one runtime call (the table lookup 16 or 32 bytes at a time, the bounds
checked once; the fastest Rust programs do the same with pshufb/tbl).
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
const out = new Uint8Array(n);
let i: int = 0;
let o: int = 0;
while (i < n) {
  const h = buf.indexOf(10, i);
  const start = h < 0 ? n : h + 1; // after the header line
  for (let t = 0; t < start - i; t++) out[o + t] = buf[i + t]; // the header line as it is (a copy)
  o += start - i;
  const nx = buf.indexOf(62, start);
  const end = nx < 0 ? n : nx;
  const total = end - start - Math.trunc((end - start + 60) / 61); // symbols (without the newlines)
  let p: int = end - 2; // the last symbol (end - 1 is the sequence's last newline), read backwards from here
  let cb: int = total % 60 === 0 ? 59 : (total % 60) - 1; // p's position within its line
  let left: int = total;
  while (left > 0) {
    const len = left < 60 ? left : 60; // the output line
    let t: int = 0;
    while (t < len) {
      // a run of the input line p is in: up to its first symbol, or what the output line still needs
      let run = cb + 1;
      if (len - t < run) run = len - t;
      const dst = o + t;
      for (let u = 0; u < run; u++) out[dst + u] = table[buf[p - u]];
      t += run;
      p -= run;
      cb -= run;
      if (cb < 0) { p--; cb = 59; } // over the newline, to the last symbol of the line before
    }
    o += len;
    out[o] = 10;
    o++;
    left -= len;
  }
  i = end;
}
process.stdout.write(out.subarray(0, o));
