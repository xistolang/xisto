/*  The Computer Language Benchmarks Game
    https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

    reverse-complement, xisto: the idiomatic program. Each sequence is a header line and its letters in lines;
    the output is the header and the reverse complement of the letters, in the same lines.
    In Node: node --import ./packages/xisto/register.mjs revcomp.ts
*/

import { readStdin, writeStdout } from "xisto:io";
import { byteMap, indexOf, reverse } from "xisto:bytes";

const COMPLEMENT = byteMap("ACGTUMRWSYKVHDBN", "TGCAAKYWSRMBDHVN", { ignoreCase: true });

function main(): void {
  const input = readStdin();
  let at = 0;
  while (at < input.length) {
    const body = indexOf(input, "\n", { from: at }) + 1;
    let end = indexOf(input, ">", { from: body });
    if (end < 0) end = input.length;
    reverse(input, { map: COMPLEMENT, skip: "\n", from: body, to: end });
    at = end;
  }
  writeStdout(input);
}

main();
