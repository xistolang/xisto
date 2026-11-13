/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

spectral-norm, xisto #6: as #5 (the denominator of A carried along the row by additions, each product split once
in equal shares of rows), with the main thread taking one of the shares itself: one worker fewer than the cores,
instead of a worker per core and a main thread that only waits. With a thread per core, the main thread waking for
the 40 barriers (a message from each worker) took a worker off its core now and then. Graviton2, 25 runs: the
slowest 1,364 s with #5, 1,250 and 1,315 in two series with #6 (Rust #5: 1,433 and 1,322); medians 1,219 against
Rust #5's 1,230. Same divisions, same order, same doubles as #3.
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

// the buffer: a slot per worker with its order (which product, its first row and the row after its last),
// then the vectors u, v and w
const SLOT: int = 3;
function vector(buf: SharedArrayBuffer, nw: int, k: int): Float64Array {
  const n = Math.trunc((buf.byteLength / 8 - Number(nw * SLOT)) / 3);
  return new Float64Array(buf, (nw * SLOT + k * n) * 8, n);
}

function mainThread(n: int): void {
  const nt = Math.max(1, Math.min(availableParallelism(), n)); // threads: this one and nt - 1 workers
  const nw = nt - 1;
  const buf = new SharedArrayBuffer((nt * SLOT + 3 * n) * 8);
  const orders = new Float64Array(buf, 0, nt * SLOT);
  const u = vector(buf, nt, 0), v = vector(buf, nt, 1);
  u.fill(1);
  // 40 products: for each of 10 rounds, v = AᵗA·u, then u = AᵗA·v; each AᵗA is A (into w), then Aᵗ
  let phase = 0, busy = 0;
  const workers: Worker[] = [];
  const share = ((Math.trunc((n + nt - 1) / nt) + 3) >> 2) << 2; // each thread's rows of a product
  // a product: a share to each worker, the last share here, then (once every worker is back) the next one
  const run = (): void => {
    while (phase < 40) {
      let next = 0;
      for (let k = 0; k < nw && next < n; k++) {
        orders[k * SLOT] = phase % 4;
        orders[k * SLOT + 1] = next;
        next = Math.min(n, next + share);
        orders[k * SLOT + 2] = next;
        workers[k].postMessage(buf);
        busy++;
      }
      const step = phase % 4;
      if (next < n) product(vector(buf, nt, step === 0 ? 0 : step === 2 ? 1 : 2), vector(buf, nt, step === 1 ? 1 : step === 3 ? 0 : 2), next, n, step === 1 || step === 3);
      phase++;
      if (busy > 0) return; // the last worker back goes on (below)
    }
    let vBv = 0, vv = 0;
    for (let i = 0; i < n; i++) {
      vBv += u[i] * v[i];
      vv += v[i] * v[i];
    }
    console.log(Math.sqrt(vBv / vv).toFixed(9));
    for (const x of workers) x.terminate();
  };
  for (let k = 0; k < nw; k++) {
    const wk = new Worker(__filename, { workerData: `${k} ${nt}` });
    workers.push(wk);
    wk.on("message", (m: string) => {
      busy--;
      if (busy === 0) run();
    });
  }
  run();
}

function workerThread(): void {
  const [ks, nws] = workerData.split(" ");
  const k = Math.trunc(Number(ks)), nw = Math.trunc(Number(nws));
  parentPort.on("message", (buf: SharedArrayBuffer) => {
    const order = new Float64Array(buf, k * SLOT * 8, SLOT);
    const step = Math.trunc(order[0]); // 0: w = A·u, 1: v = Aᵗ·w, 2: w = A·v, 3: u = Aᵗ·w
    const src = vector(buf, nw, step === 0 ? 0 : step === 2 ? 1 : 2);
    const dst = vector(buf, nw, step === 1 ? 1 : step === 3 ? 0 : 2);
    product(src, dst, Math.trunc(order[1]), Math.trunc(order[2]), step === 1 || step === 3);
    parentPort.postMessage("");
  });
}

if (isMainThread) mainThread(Math.trunc(Number(process.argv[2])));
else workerThread();
