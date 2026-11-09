// The Computer Language Benchmarks Game
// https://salsa.debian.org/benchmarksgame-team/benchmarksgame/
//
//  Contributed by Ian Osgood
//  (xisto: the random sequences from xisto:random, a generator with the program's constants and the characters
//   drawn with their probabilities; node/fasta-node-1.js is the original)

import { LinearCongruential, WeightedCharacters } from "xisto:random";

const random = new LinearCongruential({ seed: 42, multiplier: 3877, increment: 29573, modulus: 139968 });

const ALU =
  "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGG" +
  "GAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGA" +
  "CCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAAT" +
  "ACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCA" +
  "GCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGG" +
  "AGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCC" +
  "AGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA";

const IUB = new WeightedCharacters(random, "acgtBDHKMNRSVWY",
  [0.27, 0.12, 0.12, 0.27, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02]);

const HomoSap = new WeightedCharacters(random, "acgt",
  [0.3029549426680, 0.1979883004921, 0.1975473066391, 0.3015094502008]);

const LINE = 60, BLOCK = 1000 * LINE; // written a block of lines at a time

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

function fastaRandom(n: int, characters: WeightedCharacters): void {
  while (n > 0) {
    const size = Math.min(n, BLOCK);
    process.stdout.write(characters.pick(size, { lineLength: LINE }));
    n -= size;
  }
}

const n = Math.trunc(Number(process.argv[2]));

console.log(">ONE Homo sapiens alu");
fastaRepeat(2 * n, ALU);

console.log(">TWO IUB ambiguity codes");
fastaRandom(3 * n, IUB);

console.log(">THREE Homo sapiens frequency");
fastaRandom(5 * n, HomoSap);
