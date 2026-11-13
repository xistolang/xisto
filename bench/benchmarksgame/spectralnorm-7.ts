/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

spectral-norm, xisto #7: as #6 (the denominator of A carried along the row by additions; each product split once in
equal shares, the main thread taking one and a worker each of the others), with the 40 products started and joined
through two counters in the shared memory (Atomics) instead of messages: the threads go on as soon as a counter
says so, looking at it for a short while before they sleep on it (Atomics.wait), and nothing goes through the event
loop. Same divisions, same order, same doubles as #3. Graviton2, 5 series of 9 runs: medians 1,216-1,217 s (Rust
#5 1,228-1,238), one slow run in 45 (1,494); Xeon 0,455 (Rust #7 0,464).
 */

import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

// A(i, j) in doubles: every value is an exact integer below 2^53, so the result is the same as the integer
// arithmetic of #2, and nothing has to check for overflow
function A(i: number, j: number): number {
  return 1 / ((i + j) * (i + j + 1) / 2 + i + 1);
}

// dst[i] = (A·src)[i], or (Aᵗ·src)[i], for i in [lo, hi): four rows per pass, then the rows left over. Along a row
// the denominator of A grows by one more each column, so it is carried from one column to the next by additions
// instead of computed again: (i + j)(i + j + 1)/2 + i + 1 goes up by i + j + 1 (A) or by i + j + 2 (Aᵗ). Every
// value is an exact integer below 2^53, so it is the same double either way, and so is every 1 / d.
function product(src: Float64Array, dst: Float64Array, lo: int, hi: int, transposed: boolean): void {
  const n = src.length;
  const x = transposed ? 1 : 0; // Aᵗ: the step is one more
  let i = lo;
  for (; i + 3 < hi; i += 4) {
    const r = Number(i);
    // the denominators at column 0: A(r, 0) = r(r + 1)/2 + r + 1, Aᵗ(r, 0) = A(0, r) = r(r + 1)/2 + 1
    let d0 = r * (r + 1) / 2 + (transposed ? 1 : r + 1);
    let d1 = (r + 1) * (r + 2) / 2 + (transposed ? 1 : r + 2);
    let d2 = (r + 2) * (r + 3) / 2 + (transposed ? 1 : r + 3);
    let d3 = (r + 3) * (r + 4) / 2 + (transposed ? 1 : r + 4);
    let e0 = r + 1 + x, e1 = r + 2 + x, e2 = r + 3 + x, e3 = r + 4 + x; // the next steps
    let t0 = 0, t1 = 0, t2 = 0, t3 = 0;
    for (let j = 0; j < n; j++) {
      const s = src[j];
      t0 += 1 / d0 * s;
      t1 += 1 / d1 * s;
      t2 += 1 / d2 * s;
      t3 += 1 / d3 * s;
      d0 += e0; d1 += e1; d2 += e2; d3 += e3;
      e0 += 1; e1 += 1; e2 += 1; e3 += 1;
    }
    dst[i] = t0;
    dst[i + 1] = t1;
    dst[i + 2] = t2;
    dst[i + 3] = t3;
  }
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
  const share = ((Math.trunc((n + nt - 1) / nt) + 3) >> 2) << 2; // each thread's rows of a product
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
  const share = ((Math.trunc((n + nt - 1) / nt) + 3) >> 2) << 2;
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
