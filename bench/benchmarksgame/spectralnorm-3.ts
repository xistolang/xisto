/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

spectral-norm, xisto #3: #2 (rows of each product over a pool of workers sharing one SharedArrayBuffer) with
`product` taking four rows at a time: each row keeps its own sum, added in the same order as the sequential
program (so the result is the same to the last bit), but the four sums are independent, so the processor
overlaps their additions and divisions instead of waiting on one chain.
 */

import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

// A(i, j) in doubles: every value is an exact integer below 2^53, so the result is the same as the integer
// arithmetic of #2, and nothing has to check for overflow
function A(i: number, j: number): number {
  return 1 / ((i + j) * (i + j + 1) / 2 + i + 1);
}

// dst[i] = (A·src)[i], or (Aᵗ·src)[i], for i in [lo, hi): four rows per pass, then the rows left over
function product(src: Float64Array, dst: Float64Array, lo: int, hi: int, transposed: boolean): void {
  const n = src.length;
  let i = lo;
  for (; i + 3 < hi; i += 4) {
    const r0 = Number(i), r1 = r0 + 1, r2 = r0 + 2, r3 = r0 + 3;
    let t0 = 0, t1 = 0, t2 = 0, t3 = 0;
    if (transposed) {
      for (let j = 0; j < n; j++) {
        const s = src[j], c = Number(j);
        t0 += A(c, r0) * s;
        t1 += A(c, r1) * s;
        t2 += A(c, r2) * s;
        t3 += A(c, r3) * s;
      }
    } else {
      for (let j = 0; j < n; j++) {
        const s = src[j], c = Number(j);
        t0 += A(r0, c) * s;
        t1 += A(r1, c) * s;
        t2 += A(r2, c) * s;
        t3 += A(r3, c) * s;
      }
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
  const nw = Math.max(1, Math.min(availableParallelism(), n));
  const buf = new SharedArrayBuffer((nw * SLOT + 3 * n) * 8);
  const orders = new Float64Array(buf, 0, nw * SLOT);
  const u = vector(buf, nw, 0), v = vector(buf, nw, 1);
  u.fill(1);
  const chunk = Math.max(1, Math.trunc(n / (nw * 8)));
  // 40 products: for each of 10 rounds, v = AᵗA·u, then u = AᵗA·v; each AᵗA is A (into w), then Aᵗ
  let phase = 0, next = 0, busy = 0;
  const workers: Worker[] = [];
  const give = (k: int): boolean => { // the next rows of this product to worker k, if any are left
    if (next >= n) return false;
    orders[k * SLOT] = phase % 4;
    orders[k * SLOT + 1] = next;
    next = Math.min(n, next + chunk);
    orders[k * SLOT + 2] = next;
    workers[k].postMessage(buf);
    busy++;
    return true;
  };
  const start = (): void => {
    next = 0;
    for (let k = 0; k < nw; k++) give(k);
  };
  for (let k = 0; k < nw; k++) {
    const wk = new Worker(__filename, { workerData: `${k} ${nw}` });
    workers.push(wk);
    const id = k;
    wk.on("message", (m: string) => {
      busy--;
      if (give(id) || busy > 0) return;
      phase++;
      if (phase < 40) {
        start();
        return;
      }
      let vBv = 0, vv = 0;
      for (let i = 0; i < n; i++) {
        vBv += u[i] * v[i];
        vv += v[i] * v[i];
      }
      console.log(Math.sqrt(vBv / vv).toFixed(9));
      for (const x of workers) x.terminate();
    });
  }
  start();
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
