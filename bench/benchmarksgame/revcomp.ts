/*  The Computer Language Benchmarks Game
    https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

    reverse-complement, xisto: the idiomatic program. Each sequence is a header line and its letters in lines;
    the output is the header and the reverse complement of the letters, in the same lines. Each sequence is
    turned and written as soon as it has been read.
    In Node: node --import ./packages/xisto/register.mjs revcomp.ts
*/

import { readStdinParts, writeStdout } from "xisto:io";
import { byteMap, indexOf, reverse } from "xisto:bytes";

const COMPLEMENT = byteMap("ACGTUMRWSYKVHDBN", "TGCAAKYWSRMBDHVN", { ignoreCase: true });

for (const sequence of readStdinParts({ splitBefore: ">" })) {
  const body = indexOf(sequence, "\n") + 1;
  reverse(sequence, { map: COMPLEMENT, skip: "\n", from: body });
  writeStdout(sequence);
}
