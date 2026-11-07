// The Computer Language Benchmarks Game
// https://salsa.debian.org/benchmarksgame-team/benchmarksgame/
//
//  Contributed by Ian Osgood
//  (xisto: the same program with types, the counts as int; node/fasta-node-1.js is the original. The tables are a Map where the
//   original has an object used as one, iterated with for...in in the same insertion order)

let last = 42;
const A = 3877, C = 29573, M = 139968;

function rand(max: number): number {
  last = (last * A + C) % M;
  return max * last / M;
}

const ALU =
  "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGG" +
  "GAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGA" +
  "CCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAAT" +
  "ACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCA" +
  "GCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGG" +
  "AGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCC" +
  "AGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA";

const IUB = new Map<string, number>([
  ["a", 0.27], ["c", 0.12], ["g", 0.12], ["t", 0.27],
  ["B", 0.02], ["D", 0.02], ["H", 0.02], ["K", 0.02],
  ["M", 0.02], ["N", 0.02], ["R", 0.02], ["S", 0.02],
  ["V", 0.02], ["W", 0.02], ["Y", 0.02]
]);

const HomoSap = new Map<string, number>([
  ["a", 0.3029549426680],
  ["c", 0.1979883004921],
  ["g", 0.1975473066391],
  ["t", 0.3015094502008]
]);

function makeCumulative(table: Map<string, number>): void {
  let last = "";
  for (const [c, p] of table) {
    if (last !== "") table.set(c, p + table.get(last)!);
    last = c;
  }
}

function fastaRepeat(n: int, seq: string): void {
  let seqi: int = 0, lenOut: int = 60;
  while (n > 0) {
    if (n < lenOut) lenOut = n;
    if (seqi + lenOut < seq.length) {
      console.log(seq.substring(seqi, seqi + lenOut));
      seqi += lenOut;
    } else {
      const s = seq.substring(seqi);
      seqi = lenOut - s.length;
      console.log(s + seq.substring(0, seqi));
    }
    n -= lenOut;
  }
}

function fastaRandom(n: int, table: Map<string, number>): void {
  let line: string[] = new Array<string>(60).fill("");
  makeCumulative(table);
  while (n > 0) {
    if (n < line.length) line = new Array<string>(n).fill("");
    for (let i = 0; i < line.length; i++) {
      const r = rand(1);
      for (const [c, p] of table) {
        if (r < p) {
          line[i] = c;
          break;
        }
      }
    }
    console.log(line.join(""));
    n -= line.length;
  }
}

const n = Math.trunc(Number(process.argv[2]));

console.log(">ONE Homo sapiens alu");
fastaRepeat(2 * n, ALU);

console.log(">TWO IUB ambiguity codes");
fastaRandom(3 * n, IUB);

console.log(">THREE Homo sapiens frequency");
fastaRandom(5 * n, HomoSap);
