/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

fasta, xisto #2: the program written for xisto rather than translated from JS. The same generator (42,
3877, 29573, 139968) and the same choice as the JS program (the first symbol whose cumulative probability is
above last / 139968, compared in doubles), but the choice for each of the 139968 values of the generator is
worked out once into a table; lines are written 1024 at a time from a byte buffer.
 */

const IM: int = 139968;
const IA: int = 3877;
const IC: int = 29573;
const LINE: int = 60;
const LINES: int = 1024;
let last: int = 42;

const ALU =
"GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGG" +
"GAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGA" +
"CCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAAT" +
"ACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCA" +
"GCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGG" +
"AGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCC" +
"AGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA";

const buf = new Uint8Array((LINE + 1) * LINES);

function repeat(n: int, seq: string): void {
  const s = new TextEncoder().encode(seq);
  let k = 0, at = 0, col = 0;
  for (let i = 0; i < n; i++) {
    buf[at] = s[k];
    at++;
    k++;
    if (k === s.length) k = 0;
    col++;
    if (col === LINE) {
      buf[at] = 10;
      at++;
      col = 0;
      if (at === buf.length) { process.stdout.write(buf); at = 0; }
    }
  }
  if (col > 0) { buf[at] = 10; at++; }
  process.stdout.write(buf.subarray(0, at));
}

function random(n: int, symbols: string, probs: number[]): void {
  // the symbol for each value of the generator: the first whose cumulative probability is above value / IM
  const cum: number[] = [];
  let sum = 0;
  for (const p of probs) { sum += p; cum.push(sum); }
  const sym = new TextEncoder().encode(symbols);
  const table = new Uint8Array(IM);
  for (let v = 0; v < IM; v++) {
    const r = v / IM;
    let k = 0;
    while (k < cum.length - 1 && !(r < cum[k])) k++;
    table[v] = sym[k];
  }
  // four values of the generator at a time, from four independent chains: seed[i + 4] = (A4 * seed[i] + C4) % IM,
  // the same sequence, without each step waiting for the one before
  let a4: int = 1, c4: int = 0;
  for (let k = 0; k < 4; k++) { c4 = (c4 * IA + IC) % IM; a4 = (a4 * IA) % IM; }
  let v0 = (last * IA + IC) % IM, v1 = (v0 * IA + IC) % IM, v2 = (v1 * IA + IC) % IM, v3 = (v2 * IA + IC) % IM;
  let at: int = 0, col: int = 0, i: int = 0, used = last;
  while (i < n) {
    const left = n - i;
    const m = left < LINE - col ? left : LINE - col; // to the end of the line
    let k = 0;
    while (k + 4 <= m) {
      buf[at] = table[v0];
      buf[at + 1] = table[v1];
      buf[at + 2] = table[v2];
      buf[at + 3] = table[v3];
      used = v3;
      v0 = (v0 * a4 + c4) % IM;
      v1 = (v1 * a4 + c4) % IM;
      v2 = (v2 * a4 + c4) % IM;
      v3 = (v3 * a4 + c4) % IM;
      at += 4;
      k += 4;
    }
    while (k < m) { // the line's last 1-3: one value at a time, and the chains move along by one
      buf[at] = table[v0];
      used = v0;
      at++;
      k++;
      v0 = v1;
      v1 = v2;
      v2 = v3;
      v3 = (v2 * IA + IC) % IM;
    }
    i += m;
    col += m;
    if (col === LINE) {
      buf[at] = 10;
      at++;
      col = 0;
      if (at === buf.length) { process.stdout.write(buf); at = 0; }
    }
  }
  last = used; // (the next symbols start from the last value used)
  if (col > 0) { buf[at] = 10; at++; }
  process.stdout.write(buf.subarray(0, at));
}

const n: int = Math.trunc(Number(process.argv[2]));
process.stdout.write(">ONE Homo sapiens alu\n");
repeat(2 * n, ALU);
process.stdout.write(">TWO IUB ambiguity codes\n");
random(3 * n, "acgtBDHKMNRSVWY", [0.27, 0.12, 0.12, 0.27, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02]);
process.stdout.write(">THREE Homo sapiens frequency\n");
random(5 * n, "acgt", [0.3029549426680, 0.1979883004921, 0.1975473066391, 0.3015094502008]);
