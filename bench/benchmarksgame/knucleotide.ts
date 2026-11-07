/* The Computer Language Benchmarks Game
   https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

   Naive transliteration from bearophile's program
   contributed by Isaac Gouy
   (xisto: the same program with types; node/knucleotide-node-8.js is the original. It reads standard input
   line by line with node:readline; here all of it is read at once with readFileSync(0) and walked line by line,
   the same lines in the same order)
*/

import { readFileSync } from "node:fs";

function seq_lines(): string[] {
  const input = readFileSync(0, "utf8");
  const lines: string[] = [];
  let started = false;
  let at: int = 0;
  while (at < input.length) { // (each line in turn, as readline gives them)
    let end = input.indexOf("\n", at);
    if (end < 0) end = input.length;
    const line = input.substring(at, end);
    at = end + 1;
    if (started) {
      if (line.startsWith(">")) break;
      lines.push(line);
    } else {
      started = line.startsWith(">THREE");
    }
  }
  return lines;
}

function base_counts(bases: int, seq: string): Map<string, int> {
  const counts = new Map<string, int>();
  const size = seq.length + 1 - bases;
  for (let i = 0; i < size; i++) {
    const nucleo = seq.substring(i, i + bases);
    if (counts.has(nucleo)) // (the original: if (v = counts.get(nucleo)); get of an absent int is an error here)
      counts.set(nucleo, counts.get(nucleo)! + 1);
    else
      counts.set(nucleo, 1);
  }
  return counts;
}

function sorted_freq(bases: int, seq: string): [string, number][] {
  const size = seq.length + 1 - bases;
  const counts = base_counts(bases, seq);
  return Array.from(counts)
    .sort((a: [string, int], b: [string, int]) => Number(b[1] - a[1]))
    .map((a: [string, int]): [string, number] => [a[0], 100.0 * Number(a[1]) / Number(size)]);
}

function specific_count(code: string, seq: string): int {
  const counts = base_counts(code.length, seq);
  return counts.has(code) ? counts.get(code)! : 0;
}

function write_freq(a: [string, number]): void {
  console.log(`${a[0]} ${a[1].toFixed(3)}`);
}

const seq = seq_lines().map((s) => s.toUpperCase()).join("");

const bases: int[] = [1, 2];
for (const base of bases) {
  sorted_freq(base, seq).forEach(write_freq);
  console.log();
}

for (const code of ["GGT", "GGTA", "GGTATT", "GGTATTTTAATT", "GGTATTTTAATTTATAGT"]) {
  console.log(`${specific_count(code, seq)}\t${code}`);
}
