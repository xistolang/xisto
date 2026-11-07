/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

spectral-norm, xisto #2: the same power method, each matrix-vector product split by rows over a pool of
workers (node:worker_threads, one per core). Each phase sends the vector to every worker and gathers their
rows back (as JSON: a number's shortest text reads back as the same number), so every row is summed in the
same order as the sequential program and the result is the same to the last bit.
 */

import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort } from "node:worker_threads";

class Job {
  transposed: boolean = false; // A·u or Aᵗ·u
  lo: int = 0;
  hi: int = 0;
  u: number[] = [];
}

class Part {
  lo: int = 0;
  v: number[] = [];
}

function A(i: int, j: int): number {
  return 1 / (Number((i + j) * (i + j + 1)) / 2 + Number(i) + 1);
}

function rows(job: Job): Part {
  const u = job.u;
  const n = u.length;
  const v: number[] = new Array<number>(job.hi - job.lo).fill(0);
  for (let i = job.lo; i < job.hi; i++) {
    let t = 0;
    if (job.transposed) for (let j = 0; j < n; j++) t += A(j, i) * u[j];
    else for (let j = 0; j < n; j++) t += A(i, j) * u[j];
    v[i - job.lo] = t;
  }
  const p = new Part();
  p.lo = job.lo;
  p.v = v;
  return p;
}

function mainThread(n: int): void {
  const nw = Math.max(1, Math.min(availableParallelism(), n));
  const workers: Worker[] = [];
  let u: number[] = new Array<number>(n).fill(1);
  let v: number[] = new Array<number>(n).fill(0);
  let w: number[] = new Array<number>(n).fill(0);
  // 40 products: for each of 10 rounds, v = AᵗA·u, then u = AᵗA·v; each AᵗA is A (into w), then Aᵗ
  let phase = 0;
  let pending = 0;
  let next: number[] = new Array<number>(n).fill(0);
  const send = (): void => {
    const step = phase % 4; // 0: w = A·u, 1: v = Aᵗ·w, 2: w = A·v, 3: u = Aᵗ·w
    const src = step === 0 ? u : step === 2 ? v : w;
    const text = JSON.stringify(src);
    pending = nw;
    for (let k = 0; k < nw; k++) {
      const job = new Job();
      job.transposed = step === 1 || step === 3;
      job.lo = Math.trunc(n * k / nw);
      job.hi = Math.trunc(n * (k + 1) / nw);
      // the vector itself is the same text for every worker: spliced in, not encoded again
      workers[k].postMessage(`{"transposed":${job.transposed},"lo":${job.lo},"hi":${job.hi},"u":${text}}`);
    }
  };
  for (let k = 0; k < nw; k++) {
    const wk = new Worker(__filename, { workerData: "" });
    workers.push(wk);
    wk.on("message", (m: string) => {
      const p: Part = JSON.parse(m);
      for (let i = 0; i < p.v.length; i++) next[p.lo + i] = p.v[i];
      pending--;
      if (pending > 0) return;
      const step = phase % 4;
      if (step === 0 || step === 2) w = next; else if (step === 1) v = next; else u = next;
      next = new Array<number>(n).fill(0);
      phase++;
      if (phase < 40) {
        send();
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
  send();
}

function workerThread(): void {
  parentPort.on("message", (m: string) => {
    const job: Job = JSON.parse(m);
    parentPort.postMessage(JSON.stringify(rows(job)));
  });
}

if (isMainThread) mainThread(Math.trunc(Number(process.argv[2])));
else workerThread();
