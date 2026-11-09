/* The Computer Language Benchmarks Game
   https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

   k-nucleotide, xisto: the program as one writes it first. The third sequence of the input (the lines after
   ">THREE") as one string, then every substring of each length counted at once with countSubstrings (a
   Counter per length, from xisto:collections; node/knucleotide-node-8.js is the original program).
*/

import { readFileSync } from "node:fs";
import { Counter, countSubstrings } from "xisto:collections";

const CODES = ["GGT", "GGTA", "GGTATT", "GGTATTTTAATT", "GGTATTTTAATTTATAGT"];

function third(input: string): string {
  const start = input.indexOf("\n", input.indexOf(">THREE")) + 1;
  let end = input.indexOf(">", start);
  if (end < 0) end = input.length;
  return input.slice(start, end).replaceAll("\n", "").toUpperCase();
}

function printFrequencies(counts: Counter<string>, total: int): void {
  for (const [nucleotides, count] of counts.mostCommon()) {
    console.log(`${nucleotides} ${(100 * count / total).toFixed(3)}`);
  }
  console.log();
}

const seq = third(readFileSync(0, "utf8"));
const [ones, twos, ...byCode] = countSubstrings(seq, [1, 2, ...CODES.map((code) => code.length)]);
printFrequencies(ones, seq.length);
printFrequencies(twos, seq.length - 1);
CODES.forEach((code, i) => console.log(`${byCode[i].get(code)}\t${code}`));
