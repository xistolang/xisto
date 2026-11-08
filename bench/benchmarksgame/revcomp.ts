/*  The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

reverse-complement, xisto: the idiomatic program. Each sequence is a header line and its letters in lines;
the output is the header and the reverse complement of the letters, in the same lines.
xisto:io reads stdin and writes stdout as bytes; xisto:bytes reverses and complements in place, around the
line breaks (in Node, plain loops: node --import ./packages/xisto/register.mjs revcomp.ts).
 */

import { stdinBytes, writeStdout } from "xisto:io";
import { byteTable, reverseTranslateKeeping } from "xisto:bytes";

const COMPLEMENT = byteTable("ACGTUMRWSYKVHDBN", "TGCAAKYWSRMBDHVN", true);
const NL = 10, GT = 62; // '\n', '>'

function main(): void {
  const input = stdinBytes();
  let at = 0;
  while (at < input.length) {
    const body = input.indexOf(NL, at) + 1; // after the header line
    let end = input.indexOf(GT, body);
    if (end < 0) end = input.length;
    reverseTranslateKeeping(input, COMPLEMENT, NL, body, end);
    at = end;
  }
  writeStdout(input);
}

main();
