/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

fannkuch-redux, xisto #5: as #4 (720 ranges to a pool of workers; the permutation, its copies and the counters as
bytes in Uint8Array(16), kept in vector registers; a flip is one shuffle of the whole prefix), with the
permutations taken two at a time. A range starts on an even index and has an even length, and the permutation
after an even one is the same with its first two elements swapped (the sign + step), so each pair is one
permutation and that swap. The flips of a permutation are a chain: each shuffle waits for the one before, which
leaves the processor idle most of the time; the two of a pair are independent chains, counted in the same loop
while both go on (the processor runs them side by side), then whichever is left goes on alone. The checksum adds
the even one's flips and subtracts the odd one's, as before.
Xeon E5-2686 v4 (12, 2 threads, 9 runs): 6,671 s / 13,18 u (#4 7,718 / 15,24; Rust #6 7,884 / 15,63).
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
  const qa = new Uint8Array(16), qb = new Uint8Array(16);
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
  // the permutations go in pairs: the even one (idx, sign +) and the next (sign -), which is the same with its
  // first two elements swapped; the flips of the two are counted together, two independent chains of shuffles the
  // processor overlaps, then whichever has flips left goes on alone
  for (idx = idxMin; idx < idxMax; idx += 2) {
    let a0 = p[0], b0 = p[1];
    for (let j = 0; j < n; j++) qa[j] = p[j];
    qb[0] = p[1]; qb[1] = p[0];
    for (let j = 2; j < n; j++) qb[j] = p[j];
    let fa: int = 0, fb: int = 0;
    while (a0 !== 0 && b0 !== 0) {
      const na = qa[a0], nb = qb[b0];
      fa++;
      fb++;
      for (let i = 0; i < (a0 + 1) >> 1; i++) {
        const t = qa[i];
        qa[i] = qa[a0 - i];
        qa[a0 - i] = t;
      }
      for (let i = 0; i < (b0 + 1) >> 1; i++) {
        const t = qb[i];
        qb[i] = qb[b0 - i];
        qb[b0 - i] = t;
      }
      a0 = na;
      b0 = nb;
    }
    while (a0 !== 0) {
      const na = qa[a0];
      fa++;
      for (let i = 0; i < (a0 + 1) >> 1; i++) {
        const t = qa[i];
        qa[i] = qa[a0 - i];
        qa[a0 - i] = t;
      }
      a0 = na;
    }
    while (b0 !== 0) {
      const nb = qb[b0];
      fb++;
      for (let i = 0; i < (b0 + 1) >> 1; i++) {
        const t = qb[i];
        qb[i] = qb[b0 - i];
        qb[b0 - i] = t;
      }
      b0 = nb;
    }
    if (maxFlips < fa) maxFlips = fa;
    if (maxFlips < fb) maxFlips = fb;
    checkSum += fa - fb;
    // the permutation after the pair: from the odd one (p with p[0] and p[1] swapped), the - step
    let first = p[1];
    p[1] = p[0];
    p[0] = first;
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
