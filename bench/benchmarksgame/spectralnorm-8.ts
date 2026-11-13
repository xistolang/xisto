/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

spectral-norm, xisto #8: as #7, with part of each block of rows taking 1 / d from a float division and two exact
steps in doubles that give the same double as the division (exact() below), so that the divider (busy with the other
rows' double divisions) and the adders and multipliers work at once. Same 1 / d, same products, same order, same
doubles as #3 and #7. A double division costs 10 cycles for 2 on Neoverse-N1 and 8 for 4 on Broadwell, a float one
6 for 4 and 5 for 8. 9 runs interleaved (5500, 2 threads, XISTO_CPU=native): Graviton2 0,873 s / 1,74 s of CPU
(#7 1,216; Rust #5 1,230 / 2,45); Xeon E5-2686 v4 0,426 s / 0,85 (#7 0,456; Rust #7 0,462 / 0,89).
The check of exact(), in C (-ffp-contract=off): for every d in 1..2^29, double q = (float)(1.0f / (float)d),
r = 1 - d*q, y = q*r, s = q + y, x = s + (((q - s) + y) + y*(r + r*r)); x == 1.0 / d bit for bit (0 differences;
without the r*r term 267 differ). And (float)(1.0 / (double)(float)d) == 1.0f / (float)d for all of them (a double
rounded to float: Node's Math.fround of the double quotient is the float quotient).
 Not scored (bench/placar.py, XISTO_NOT_THE_SAME): the problem asks for the double division, and the
scoreboard keeps the Benchmarks Game results as the problem states them. Kept as a measured experiment.
 */

import { arch, availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

// A(i, j) in doubles: every value is an exact integer below 2^53, so the result is the same as the integer
// arithmetic of #2, and nothing has to check for overflow
function A(i: number, j: number): number {
  return 1 / ((i + j) * (i + j + 1) / 2 + i + 1);
}


// 1 / d, the same double as the division, for an integer d in [1, 2^29]: q = the float 1 / d (2^-24 off at most),
// r = 1 - d·q exact (d·q has at most 29 + 24 bits, and is within 2^-23 of 1), so 1 / d = q / (1 - r) =
// q + q·r + q·r² + ...; y = q·r is exact (24 + 29 bits), q + y is split into s and its rounding error (q is the
// larger), and the terms after it go into that error before the one rounding of s. Checked against 1 / d for every
// integer d from 1 to 2^29 (the denominators of A go up to about 2n², 2^29 at n = 16384)
function exact(d: number): number {
  const q = Math.fround(1 / Math.fround(d));
  const r = 1 - d * q;
  const y = q * r;
  const s = q + y;
  return s + (((q - s) + y) + y * (r + r * r));
}

// dst[i] = (A·src)[i], or (Aᵗ·src)[i], for i in [lo, hi), a block of rows per pass, then the rows left over. Along
// a row the denominator of A grows by one more each column, so it is carried from one column to the next by
// additions: (i + j)(i + j + 1)/2 + i + 1 goes up by i + j + 1 (A) or by i + j + 2 (Aᵗ). Every value is an exact
// integer below 2^53. In a block, the first rows divide, the last ones go through exact(): the divider is busy with
// the first while the adders and multipliers do the second. The shape suits the vectors of each machine: on arm64
// (2 doubles a vector, the divider 10 cycles for 2) 4 rows divide and 2 go through exact(); on x86-64 with AVX (4
// doubles, 8 cycles for 4) 8 and 4. A block's sums go to `block` together and then into dst at once: stored one
// by one into dst (each store checked) the C compiler kept the rows apart, scalar (Graviton2 1,55 s)
function rows6(src: Float64Array, dst: Float64Array, lo: int, hi: int, transposed: boolean): int {
  const n = src.length;
  const x = transposed ? 1 : 0;
  const block = new Float64Array(6); // the block's results, stored together then copied
  let i = lo;
  for (; i + 5 < hi; i += 6) {
    const r = Number(i);
    let d0 = (r + 0) * (r + 1) / 2 + (transposed ? 1 : r + 1), e0 = r + 1 + x, t0 = 0;
    let d1 = (r + 1) * (r + 2) / 2 + (transposed ? 1 : r + 2), e1 = r + 2 + x, t1 = 0;
    let d2 = (r + 2) * (r + 3) / 2 + (transposed ? 1 : r + 3), e2 = r + 3 + x, t2 = 0;
    let d3 = (r + 3) * (r + 4) / 2 + (transposed ? 1 : r + 4), e3 = r + 4 + x, t3 = 0;
    let d4 = (r + 4) * (r + 5) / 2 + (transposed ? 1 : r + 5), e4 = r + 5 + x, t4 = 0;
    let d5 = (r + 5) * (r + 6) / 2 + (transposed ? 1 : r + 6), e5 = r + 6 + x, t5 = 0;
    for (let j = 0; j < n; j++) {
      const s = src[j];
      t0 += 1 / d0 * s;
      t1 += 1 / d1 * s;
      t2 += 1 / d2 * s;
      t3 += 1 / d3 * s;
      t4 += exact(d4) * s;
      t5 += exact(d5) * s;
      d0 += e0; d1 += e1; d2 += e2; d3 += e3; d4 += e4; d5 += e5;
      e0 += 1; e1 += 1; e2 += 1; e3 += 1; e4 += 1; e5 += 1;
    }
    block[0] = t0;
    block[1] = t1;
    block[2] = t2;
    block[3] = t3;
    block[4] = t4;
    block[5] = t5;
    dst.set(block, i);
  }
  return i;
}

function rows12(src: Float64Array, dst: Float64Array, lo: int, hi: int, transposed: boolean): int {
  const n = src.length;
  const x = transposed ? 1 : 0;
  const block = new Float64Array(12); // the block's results, stored together then copied
  let i = lo;
  for (; i + 11 < hi; i += 12) {
    const r = Number(i);
    let d0 = (r + 0) * (r + 1) / 2 + (transposed ? 1 : r + 1), e0 = r + 1 + x, t0 = 0;
    let d1 = (r + 1) * (r + 2) / 2 + (transposed ? 1 : r + 2), e1 = r + 2 + x, t1 = 0;
    let d2 = (r + 2) * (r + 3) / 2 + (transposed ? 1 : r + 3), e2 = r + 3 + x, t2 = 0;
    let d3 = (r + 3) * (r + 4) / 2 + (transposed ? 1 : r + 4), e3 = r + 4 + x, t3 = 0;
    let d4 = (r + 4) * (r + 5) / 2 + (transposed ? 1 : r + 5), e4 = r + 5 + x, t4 = 0;
    let d5 = (r + 5) * (r + 6) / 2 + (transposed ? 1 : r + 6), e5 = r + 6 + x, t5 = 0;
    let d6 = (r + 6) * (r + 7) / 2 + (transposed ? 1 : r + 7), e6 = r + 7 + x, t6 = 0;
    let d7 = (r + 7) * (r + 8) / 2 + (transposed ? 1 : r + 8), e7 = r + 8 + x, t7 = 0;
    let d8 = (r + 8) * (r + 9) / 2 + (transposed ? 1 : r + 9), e8 = r + 9 + x, t8 = 0;
    let d9 = (r + 9) * (r + 10) / 2 + (transposed ? 1 : r + 10), e9 = r + 10 + x, t9 = 0;
    let d10 = (r + 10) * (r + 11) / 2 + (transposed ? 1 : r + 11), e10 = r + 11 + x, t10 = 0;
    let d11 = (r + 11) * (r + 12) / 2 + (transposed ? 1 : r + 12), e11 = r + 12 + x, t11 = 0;
    for (let j = 0; j < n; j++) {
      const s = src[j];
      t0 += 1 / d0 * s;
      t1 += 1 / d1 * s;
      t2 += 1 / d2 * s;
      t3 += 1 / d3 * s;
      t4 += 1 / d4 * s;
      t5 += 1 / d5 * s;
      t6 += 1 / d6 * s;
      t7 += 1 / d7 * s;
      t8 += exact(d8) * s;
      t9 += exact(d9) * s;
      t10 += exact(d10) * s;
      t11 += exact(d11) * s;
      d0 += e0; d1 += e1; d2 += e2; d3 += e3; d4 += e4; d5 += e5; d6 += e6; d7 += e7; d8 += e8; d9 += e9; d10 += e10; d11 += e11;
      e0 += 1; e1 += 1; e2 += 1; e3 += 1; e4 += 1; e5 += 1; e6 += 1; e7 += 1; e8 += 1; e9 += 1; e10 += 1; e11 += 1;
    }
    block[0] = t0;
    block[1] = t1;
    block[2] = t2;
    block[3] = t3;
    block[4] = t4;
    block[5] = t5;
    block[6] = t6;
    block[7] = t7;
    block[8] = t8;
    block[9] = t9;
    block[10] = t10;
    block[11] = t11;
    dst.set(block, i);
  }
  return i;
}

const ARM: boolean = arch() === "arm64";
function product(src: Float64Array, dst: Float64Array, lo: int, hi: int, transposed: boolean): void {
  const n = src.length;
  let i = lo;
  // exact() holds while every denominator is at most 2^29: the largest is A(n - 1, n - 1) = (2n - 2)(2n - 1)/2 + n
  const m = Number(n);
  if ((2 * m - 2) * (2 * m - 1) / 2 + m <= 536870912) i = ARM ? rows6(src, dst, lo, hi, transposed) : rows12(src, dst, lo, hi, transposed);
  for (; i < hi; i++) {
    const r = Number(i);
    let t = 0;
    if (transposed) for (let j = 0; j < n; j++) t += A(Number(j), r) * src[j];
    else for (let j = 0; j < n; j++) t += A(r, Number(j)) * src[j];
    dst[i] = t;
  }
}

// the buffer: two counters (int32: the products started, the shares done), then the vectors u, v and w
const SPIN: int = 1000000; // looks at a counter this many times (a few ms) before sleeping on it (Atomics.wait)
function vector(buf: SharedArrayBuffer, k: int): Float64Array {
  const n = Math.trunc((buf.byteLength - 8) / 24);
  return new Float64Array(buf, 8 + k * n * 8, n);
}
// product p (0: w = A·u, 1: v = Aᵗ·w, 2: w = A·v, 3: u = Aᵗ·w) on rows [lo, hi)
function step(buf: SharedArrayBuffer, p: int, lo: int, hi: int): void {
  const s = p % 4;
  product(vector(buf, s === 0 ? 0 : s === 2 ? 1 : 2), vector(buf, s === 1 ? 1 : s === 3 ? 0 : 2), lo, hi, s === 1 || s === 3);
}
// until counter i is at least target: a short look first, then asleep until it changes
function waitFor(c: Int32Array, i: int, target: int): void {
  for (let k = 0; k < SPIN; k++) if (Atomics.load(c, i) >= target) return;
  for (;;) {
    const now = Atomics.load(c, i);
    if (now >= target) return;
    Atomics.wait(c, i, now);
  }
}

function mainThread(n: int): void {
  const nt = Math.max(1, Math.min(availableParallelism(), n)); // threads: this one and nt - 1 workers
  const buf = new SharedArrayBuffer(8 + 3 * n * 8);
  const c = new Int32Array(buf, 0, 2);
  const u = vector(buf, 0), v = vector(buf, 1);
  u.fill(1);
  const share = Math.trunc((Math.trunc((n + nt - 1) / nt) + 11) / 12) * 12; // each thread's rows of a product
  const workers: Worker[] = [];
  for (let k = 0; k < nt - 1; k++) {
    const wk = new Worker(__filename, { workerData: `${k} ${nt} ${n}` });
    workers.push(wk);
    wk.postMessage(buf);
  }
  // 40 products: for each of 10 rounds, v = AᵗA·u, then u = AᵗA·v; each AᵗA is A (into w), then Aᵗ. A product
  // starts when counter 0 says so; this thread takes the last share and waits on counter 1 for the workers' shares
  for (let p = 0; p < 40; p++) {
    Atomics.store(c, 0, p + 1);
    Atomics.notify(c, 0);
    const lo = Math.min(n, (nt - 1) * share);
    if (lo < n) step(buf, p, lo, n);
    waitFor(c, 1, (p + 1) * (nt - 1));
  }
  let vBv = 0, vv = 0;
  for (let i = 0; i < n; i++) {
    vBv += u[i] * v[i];
    vv += v[i] * v[i];
  }
  console.log(Math.sqrt(vBv / vv).toFixed(9));
  for (const x of workers) x.terminate();
}

function workerThread(): void {
  const [ks, nts, ns] = workerData.split(" ");
  const k = Math.trunc(Number(ks)), nt = Math.trunc(Number(nts)), n = Math.trunc(Number(ns));
  const share = Math.trunc((Math.trunc((n + nt - 1) / nt) + 11) / 12) * 12;
  parentPort.on("message", (buf: SharedArrayBuffer) => {
    const c = new Int32Array(buf, 0, 2);
    const lo = Math.min(n, k * share), hi = Math.min(n, lo + share);
    for (let p = 0; p < 40; p++) {
      waitFor(c, 0, p + 1);
      if (lo < hi) step(buf, p, lo, hi);
      Atomics.add(c, 1, 1);
      Atomics.notify(c, 1);
    }
  });
}

if (isMainThread) mainThread(Math.trunc(Number(process.argv[2])));
else workerThread();
