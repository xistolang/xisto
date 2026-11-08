/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

fannkuch-redux, xisto #4: as #3 (720 ranges to a pool of workers; the permutation, its copy and the counters as
bytes in Uint8Array(16), which the emitter keeps in a vector register for the loop), with the flip as the Rust
program does it: the whole prefix [0..p0] reversed in one shuffle, so the first element of the copy is the
one being flipped and nothing has to be written back after the reversal.
 */

import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

class Range {
  idxMin: int = 0;
  idxMax: int = 0;
}

class Result {
  maxFlips: int = 0;
  checkSum: int = 0;
}

function factorials(n: int): int[] {
  const fact: int[] = new Array<int>(n + 1).fill(1);
  for (let i = 1; i <= n; i++) fact[i] = fact[i - 1] * i;
  return fact;
}

function fannkuch(n: int, fact: int[], idxMin: int, idxMax: int): Result {
  const p = new Uint8Array(16);
  const pp = new Uint8Array(16);
  const count = new Uint8Array(16);
  // the first permutation of the range
  for (let i = 0; i < n; i++) p[i] = i;
  let idx = idxMin;
  for (let i = n - 1; i > 0; i--) {
    const d = Math.trunc(idx / fact[i]);
    count[i] = d;
    idx = idx % fact[i];
    for (let j = 0; j < n; j++) pp[j] = p[j];
    for (let j = 0; j <= i; j++) p[j] = j + d <= i ? pp[j + d] : pp[j + d - i - 1];
  }
  let maxFlips: int = 1;
  let checkSum: int = 0;
  idx = idxMin;
  for (let sign = true; ; sign = !sign) {
    let first = p[0];
    if (first !== 0) {
      let flips: int = 1;
      if (p[first] !== 0) {
        for (let j = 0; j < n; j++) pp[j] = p[j];
        let p0 = first;
        while (true) {
          const next = pp[p0];
          if (next === 0) break;
          flips++;
          // pp[0..p0] reversed: i and p0 - i trade places while i < p0 - i (one shuffle, see xs_u8_rev); the
          // old pp[0], which is p0, lands at pp[p0] by itself, and the new pp[0] is next
          for (let i = 0; i < (p0 + 1) >> 1; i++) {
            const t = pp[i];
            pp[i] = pp[p0 - i];
            pp[p0 - i] = t;
          }
          p0 = next;
        }
      }
      if (maxFlips < flips) maxFlips = flips;
      if (sign) checkSum += flips;
      else checkSum -= flips;
    }
    idx++;
    if (idx === idxMax) break;
    // the next permutation
    if (sign) {
      p[0] = p[1];
      p[1] = first;
    } else {
      const t = p[1];
      p[1] = p[2];
      p[2] = t;
      for (let k = 2; ; k++) {
        count[k]++;
        if (count[k] <= k) break;
        count[k] = 0;
        for (let j = 0; j <= k; j++) p[j] = p[j + 1];
        p[k + 1] = first;
        first = p[0];
      }
    }
  }
  const r = new Result();
  r.maxFlips = maxFlips;
  r.checkSum = checkSum;
  return r;
}

function mainThread(n: int): void {
  const fact = factorials(n);
  const nchunks: int = 720;
  let chunkSize: int = Math.trunc((fact[n] + nchunks - 1) / nchunks);
  chunkSize += chunkSize % 2;
  const tasks: Range[] = [];
  const len = Math.trunc((fact[n] + chunkSize - 1) / chunkSize);
  for (let i = 0; i < len; i++) {
    const r = new Range();
    r.idxMin = chunkSize * i;
    r.idxMax = Math.min(fact[n], r.idxMin + chunkSize);
    tasks.push(r);
  }
  let flips: int = 0;
  let chk: int = 0;
  let next = 0;
  let left = tasks.length;
  const size = Math.min(availableParallelism(), tasks.length);
  for (let k = 0; k < size; k++) {
    const w = new Worker(__filename, { workerData: `${n}` });
    w.on("message", (m: string) => {
      const res: Result = JSON.parse(m);
      if (flips < res.maxFlips) flips = res.maxFlips;
      chk += res.checkSum;
      left--;
      if (next < tasks.length) {
        w.postMessage(JSON.stringify(tasks[next]));
        next++;
      } else w.terminate();
      if (left === 0) console.log(`${chk}\nPfannkuchen(${n}) = ${flips}`);
    });
    w.postMessage(JSON.stringify(tasks[next]));
    next++;
  }
}

function workerThread(): void {
  const n: int = Math.trunc(Number(workerData));
  const fact = factorials(n);
  parentPort.on("message", (m: string) => {
    const r: Range = JSON.parse(m);
    parentPort.postMessage(JSON.stringify(fannkuch(n, fact, r.idxMin, r.idxMax)));
  });
}

if (isMainThread) mainThread(Math.trunc(Number(process.argv[2])));
else workerThread();
