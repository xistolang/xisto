/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

reverse-complement, xisto #3: as #2 (the whole input at once, each sequence complemented in place, backwards,
through a 256-byte table, the newlines every 60 symbols left where they are), with the work shared: the input
lives on a SharedArrayBuffer that every worker (node:worker_threads, one per core) sees as the same memory;
each sequence's symbol pairs (j from the front with total-1-j from the back) are split into one range per
worker, so the workers write disjoint parts of the buffer; a sequence is written out as soon as its ranges are
done, while the workers go on with the next one.

Measured (8 nov 2026): slower than #2 everywhere (Xeon 0,28 -> 0,48 s, Graviton2 0,32 -> 0,38, M4 0,09 -> 0,10):
the copy of the 254 MB input into the SharedArrayBuffer (a second 254 MB of page faults: RSS 250 -> 498 MB) costs
more than the shared computing saves (0,06 cpu-s). It would pay only with the input read in blocks straight
into the shared memory (fs.readSync into a given buffer), which xisto does not have yet. Kept as the record.
 */

import { readFileSync } from "node:fs";
import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

function complementTable(): Uint8Array {
  const t = new Uint8Array(256);
  for (let i = 0; i < 256; i++) t[i] = i;
  const pairs = new TextEncoder().encode("ATCGGCTAUAMKRYWWSSYRKMVBHDDHBVNN");
  for (let i = 0; i < pairs.length; i += 2) {
    t[pairs[i]] = pairs[i + 1];
    t[pairs[i] + 32] = pairs[i + 1]; // the lower case too
  }
  return t;
}
const table = complementTable();

// symbols lo..hi-1 of a sequence of total symbols starting at start, each traded (complemented) with its
// mirror total-1-j; the newlines every 60 symbols are skipped, never touched
function trade(buf: Uint8Array, start: int, total: int, lo: int, hi: int): void {
  let a: int = start + lo + Math.trunc(lo / 60);
  let ca: int = lo % 60; // a's position within its line
  const m: int = total - 1 - lo;
  let b: int = start + m + Math.trunc(m / 60);
  let cb: int = m % 60; // b's position within its line
  let left: int = hi - lo;
  while (left > 0) {
    // a run where neither cursor reaches the end of its line: no newline to skip inside it
    let run: int = 60 - ca;
    if (cb + 1 < run) run = cb + 1;
    if (left < run) run = left;
    for (let t = 0; t < run; t++) {
      const x = buf[a + t];
      buf[a + t] = table[buf[b - t]];
      buf[b - t] = table[x];
    }
    a += run;
    b -= run;
    ca += run;
    cb -= run;
    left -= run;
    if (ca === 60) { a++; ca = 0; }
    if (cb < 0) { b--; cb = 59; } // over the newline, to the last symbol of the line before
  }
}

function put32(t: Uint8Array, at: int, v: int): void {
  t[at] = v & 0xff;
  t[at + 1] = (v >> 8) & 0xff;
  t[at + 2] = (v >> 16) & 0xff;
  t[at + 3] = (v >> 24) & 0xff;
}
function get32(t: Uint8Array, at: int): int {
  return t[at] | (t[at + 1] << 8) | (t[at + 2] << 16) | (t[at + 3] << 24);
}

function mainThread(): void {
  const input = readFileSync(0);
  const n = input.length;
  const buf = new Uint8Array(new SharedArrayBuffer(n));
  buf.set(input);
  // the sequences: header start, body start, end, symbols
  const hdr: int[] = [];
  const bodyStart: int[] = [];
  const bodyEnd: int[] = [];
  const symbols: int[] = [];
  let i: int = 0;
  while (i < n) {
    const h = buf.indexOf(10, i);
    const start = h < 0 ? n : h + 1; // after the header line
    const nx = buf.indexOf(62, start);
    const end = nx < 0 ? n : nx;
    hdr.push(i);
    bodyStart.push(start);
    bodyEnd.push(end);
    symbols.push(end - start - Math.trunc((end - start + 60) / 61)); // symbols (without the newlines)
    i = end;
  }
  const nseq = hdr.length;
  const nworkers = Math.max(1, Math.min(availableParallelism(), 32));
  // one range of pairs per worker and sequence, in sequence order; the odd middle symbol is the main's
  const taskSeq: int[] = [];
  const taskLo: int[] = [];
  const taskHi: int[] = [];
  const pending: int[] = new Array<int>(nseq).fill(0);
  for (let s = 0; s < nseq; s++) {
    const half = Math.trunc(symbols[s] / 2);
    const parts = Math.min(nworkers, Math.max(1, Math.trunc(half / 4096)));
    for (let p = 0; p < parts; p++) {
      const lo = Math.trunc(Number(half) * Number(p) / Number(parts));
      const hi = Math.trunc(Number(half) * Number(p + 1) / Number(parts));
      if (hi <= lo) continue;
      taskSeq.push(s);
      taskLo.push(lo);
      taskHi.push(hi);
      pending[s]++;
    }
    if (symbols[s] % 2 === 1) {
      const mid = half;
      const at = bodyStart[s] + mid + Math.trunc(mid / 60);
      buf[at] = table[buf[at]];
    }
  }
  const ntasks = taskSeq.length;
  let next = 0, written = 0;
  const flush = (): void => {
    while (written < nseq && pending[written] === 0) {
      process.stdout.write(buf.subarray(hdr[written], bodyEnd[written]));
      written++;
    }
  };
  if (ntasks === 0) { flush(); return; }
  const n_w = Math.min(nworkers, ntasks);
  for (let w = 0; w < n_w; w++) {
    const wk = new Worker(__filename, { workerData: "" });
    const give = (): boolean => {
      if (next >= ntasks) return false;
      const t = new Uint8Array(16);
      put32(t, 0, taskSeq[next]);
      put32(t, 4, bodyStart[taskSeq[next]]);
      put32(t, 8, taskLo[next]);
      put32(t, 12, taskHi[next]);
      next++;
      wk.postMessage(t);
      return true;
    };
    wk.on("message", (r: Uint8Array) => {
      const s = get32(r, 0);
      pending[s]--;
      if (!give()) wk.terminate();
      flush();
    });
    wk.postMessage(buf); // the input first (shared memory), then one range at a time
    give();
  }
}

function workerThread(): void {
  let buf: Uint8Array | null = null;
  const totals: Map<int, int> = new Map<int, int>();
  parentPort.on("message", (m: Uint8Array) => {
    if (buf === null) { buf = m; return; } // the first message is the input
    const s = get32(m, 0);
    const start = get32(m, 4);
    const lo = get32(m, 8);
    const hi = get32(m, 12);
    // the sequence's symbol count, from the buffer itself (up to the next header or the end)
    let total: int = 0;
    if (totals.has(s)) total = totals.get(s)!;
    else {
      const nx = buf.indexOf(62, start);
      const end = nx < 0 ? buf.length : nx;
      total = end - start - Math.trunc((end - start + 60) / 61);
      totals.set(s, total);
    }
    trade(buf, start, total, lo, hi);
    const r = new Uint8Array(4);
    put32(r, 0, s);
    parentPort.postMessage(r);
  });
}

if (isMainThread) mainThread();
else workerThread();
