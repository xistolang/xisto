/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

reverse-complement, xisto #4: the input is read in blocks (fs.readSync) straight into a SharedArrayBuffer sized
by fstatSync, so there is no second copy; as each sequence is complete (its next header, or the end, has been
read) its symbol pairs are split into one range per worker (node:worker_threads), which complement them in
place while the main thread goes on reading; the sequences are written out in order as their ranges come back.
Each sequence's complement is written backwards through a 256-byte table, the newlines every 60 symbols left
where they are (the same loop as #2). When stdin is not a file (a pipe), all of it is read first, as in #3.
 */

import { readFileSync, readSync, fstatSync } from "node:fs";
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

const BLOCK: int = 1 << 20;

function mainThread(): void {
  const st = fstatSync(0);
  const streamed = st.isFile() && st.size > 0;
  let n: int = streamed ? st.size : 0;
  const input = streamed ? new Uint8Array(0) : readFileSync(0); // a pipe: all of it first
  if (!streamed) n = input.length;
  const buf = new Uint8Array(new SharedArrayBuffer(n));
  if (!streamed) buf.set(input);
  const nworkers = Math.max(1, Math.min(availableParallelism(), 32));
  const workers: Worker[] = [];
  // a sequence: header start, body start, end (the next header or n); its ranges pending; written in order
  const hdr: int[] = [];
  const bodyStart: int[] = [];
  const bodyEnd: int[] = [];
  const pending: int[] = [];
  let written = 0;
  const flush = (): void => {
    while (written < hdr.length && pending[written] === 0 && bodyEnd[written] >= 0) {
      process.stdout.write(buf.subarray(hdr[written], bodyEnd[written]));
      written++;
    }
  };
  let left = 0; // ranges still to come back
  let allSent = false;
  for (let w = 0; w < nworkers; w++) {
    const wk = new Worker(__filename, { workerData: "" });
    wk.on("message", (r: Uint8Array) => {
      pending[get32(r, 0)]--;
      left--;
      flush();
      if (allSent && left === 0) for (const k of workers) k.terminate();
    });
    wk.postMessage(buf); // the shared memory first, then ranges
    workers.push(wk);
  }
  let rr = 0; // the next worker to take a range
  // sequence s is complete: its body ends at end; split its pairs into ranges and hand them out
  const complete = (s: int, end: int): void => {
    bodyEnd[s] = end;
    const start = bodyStart[s];
    const total = end - start - Math.trunc((end - start + 60) / 61); // symbols (without the newlines)
    const half = Math.trunc(total / 2);
    const parts = Math.min(nworkers, Math.max(1, Math.trunc(half / 4096)));
    for (let p = 0; p < parts; p++) {
      const lo = Math.trunc(Number(half) * Number(p) / Number(parts));
      const hi = Math.trunc(Number(half) * Number(p + 1) / Number(parts));
      if (hi <= lo) continue;
      const t = new Uint8Array(20);
      put32(t, 0, s);
      put32(t, 4, start);
      put32(t, 8, total);
      put32(t, 12, lo);
      put32(t, 16, hi);
      pending[s]++;
      left++;
      workers[rr].postMessage(t);
      rr = (rr + 1) % nworkers;
    }
    if (total % 2 === 1) { // the middle symbol is the main's
      const mid = half;
      const at = start + mid + Math.trunc(mid / 60);
      buf[at] = table[buf[at]];
    }
  };
  // read (or scan) block by block: a '>' opens a sequence and completes the one before it
  let have: int = streamed ? 0 : n; // bytes in the buffer
  let scanned: int = 0; // bytes looked at for headers
  let inHeader = false; // between a '>' and its newline
  while (true) {
    if (streamed && have < n) {
      const got = readSync(0, buf, have, Math.min(BLOCK, n - have), null);
      if (got <= 0) { n = have; break; } // shorter than announced: what we have is the input
      have += got;
    }
    while (scanned < have) {
      // (searched only in the bytes read so far: past them the buffer is still zeros up to the end)
      const fresh = buf.subarray(scanned, have);
      if (inHeader) {
        const nl = fresh.indexOf(10, 0);
        if (nl < 0) { scanned = have; break; }
        bodyStart[hdr.length - 1] = scanned + nl + 1;
        inHeader = false;
        scanned = scanned + nl + 1;
      } else {
        const rel = fresh.indexOf(62, 0);
        if (rel < 0) { scanned = have; break; }
        const gt = scanned + rel;
        if (hdr.length > 0) complete(hdr.length - 1, gt);
        hdr.push(gt);
        bodyStart.push(-1);
        bodyEnd.push(-1);
        pending.push(0);
        inHeader = true;
        scanned = gt + 1;
      }
    }
    if (have >= n) break;
  }
  if (hdr.length > 0) {
    if (bodyStart[hdr.length - 1] < 0) bodyStart[hdr.length - 1] = n; // a header without a body
    complete(hdr.length - 1, n);
  }
  allSent = true;
  flush();
  if (left === 0) for (const k of workers) k.terminate();
}

function workerThread(): void {
  let buf: Uint8Array | null = null;
  parentPort.on("message", (m: Uint8Array) => {
    if (buf === null) { buf = m; return; } // the first message is the shared memory
    const s = get32(m, 0);
    trade(buf, get32(m, 4), get32(m, 8), get32(m, 12), get32(m, 16));
    const r = new Uint8Array(4);
    put32(r, 0, s);
    parentPort.postMessage(r);
  });
}

if (isMainThread) mainThread();
else workerThread();
