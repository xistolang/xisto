/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

reverse-complement, xisto #5: as #4 (the input read in blocks straight into a SharedArrayBuffer, each complete
sequence's symbol pairs split into one range per worker, written out in order), with the reading moved to a
worker of its own: the reader reads stdin block by block into the shared memory and tells the main thread
each sequence that is complete; the main thread, free in its event loop, hands the ranges to the computing
workers and writes every finished sequence from the shared memory while the reader is still reading the next
ones. Reading, computing and writing overlap, with no second buffer. When stdin is not a file (a pipe), the
main thread reads all of it first.
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

// a message to the main thread: a complete sequence (header start, body start, end), or the end of the input
function announce(s: int, hdr: int, bodyStart: int, end: int): void {
  const t = new Uint8Array(16);
  put32(t, 0, s);
  put32(t, 4, hdr);
  put32(t, 8, bodyStart);
  put32(t, 12, end);
  parentPort.postMessage(t);
}

// the reader: stdin in blocks into the shared memory; a '>' opens a sequence and completes the one before it
function readerThread(): void {
  parentPort.on("message", (m: Uint8Array) => {
    const buf = m;
    const n: int = buf.length;
    let have: int = 0; // bytes in the buffer
    let scanned: int = 0; // bytes looked at for headers
    let inHeader = false; // between a '>' and its newline
    let nseq: int = 0;
    let hdrAt: int = 0, bodyAt: int = -1;
    while (true) {
      if (have < n) {
        const got = readSync(0, buf, have, Math.min(BLOCK, n - have), null);
        if (got <= 0) break; // shorter than announced: what we have is the input
        have += got;
      }
      while (scanned < have) {
        // (searched only in the bytes read so far: past them the buffer is still zeros up to the end)
        const fresh = buf.subarray(scanned, have);
        if (inHeader) {
          const nl = fresh.indexOf(10, 0);
          if (nl < 0) { scanned = have; break; }
          bodyAt = scanned + nl + 1;
          inHeader = false;
          scanned = scanned + nl + 1;
        } else {
          const rel = fresh.indexOf(62, 0);
          if (rel < 0) { scanned = have; break; }
          const gt = scanned + rel;
          if (nseq > 0) announce(nseq - 1, hdrAt, bodyAt < 0 ? gt : bodyAt, gt);
          hdrAt = gt;
          bodyAt = -1;
          nseq++;
          inHeader = true;
          scanned = gt + 1;
        }
      }
      if (have >= n) break;
    }
    if (nseq > 0) announce(nseq - 1, hdrAt, bodyAt < 0 ? have : bodyAt, have);
    announce(-1, have, 0, 0); // the end: how much was read
  });
}

function mainThread(): void {
  const st = fstatSync(0);
  const streamed = st.isFile() && st.size > 0;
  let n: int = streamed ? st.size : 0;
  const input = streamed ? new Uint8Array(0) : readFileSync(0); // a pipe: all of it first, here
  if (!streamed) n = input.length;
  const buf = new Uint8Array(new SharedArrayBuffer(n));
  if (!streamed) buf.set(input);
  const nworkers = Math.max(1, Math.min(availableParallelism(), 32));
  const workers: Worker[] = [];
  // a sequence: header start, body start, end; its ranges pending; written in order
  const hdr: int[] = [];
  const bodyStart: int[] = [];
  const bodyEnd: int[] = [];
  const pending: int[] = [];
  let written = 0;
  let left = 0; // ranges still to come back
  let allSent = false;
  const flush = (): void => {
    while (written < hdr.length && pending[written] === 0) {
      process.stdout.write(buf.subarray(hdr[written], bodyEnd[written]));
      written++;
    }
  };
  const finish = (): void => {
    if (allSent && left === 0) for (const k of workers) k.terminate();
  };
  for (let w = 0; w < nworkers; w++) {
    const wk = new Worker(__filename, { workerData: "c" });
    wk.on("message", (r: Uint8Array) => {
      pending[get32(r, 0)]--;
      left--;
      flush();
      finish();
    });
    wk.postMessage(buf); // the shared memory first, then ranges
    workers.push(wk);
  }
  let rr = 0; // the next worker to take a range
  // sequence s is complete: split its pairs into ranges and hand them out; the middle symbol is the main's
  const complete = (s: int, h: int, start: int, end: int): void => {
    hdr.push(h);
    bodyStart.push(start);
    bodyEnd.push(end);
    pending.push(0);
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
    if (total % 2 === 1) {
      const mid = half;
      const at = start + mid + Math.trunc(mid / 60);
      buf[at] = table[buf[at]];
    }
  };
  if (streamed) {
    const reader = new Worker(__filename, { workerData: "r" });
    reader.on("message", (m: Uint8Array) => {
      const s = get32(m, 0);
      if (s < 0) { // the end
        allSent = true;
        reader.terminate();
        flush();
        finish();
        return;
      }
      complete(s, get32(m, 4), get32(m, 8), get32(m, 12));
      flush();
    });
    reader.postMessage(buf);
    return;
  }
  // a pipe: the input is all here; the same scan, in this thread
  let scanned: int = 0, inHeader = false, nseq: int = 0, hdrAt: int = 0, bodyAt: int = -1;
  while (scanned < n) {
    const fresh = buf.subarray(scanned, n);
    if (inHeader) {
      const nl = fresh.indexOf(10, 0);
      if (nl < 0) break;
      bodyAt = scanned + nl + 1;
      inHeader = false;
      scanned = scanned + nl + 1;
    } else {
      const rel = fresh.indexOf(62, 0);
      if (rel < 0) break;
      const gt = scanned + rel;
      if (nseq > 0) complete(nseq - 1, hdrAt, bodyAt < 0 ? gt : bodyAt, gt);
      hdrAt = gt;
      bodyAt = -1;
      nseq++;
      inHeader = true;
      scanned = gt + 1;
    }
  }
  if (nseq > 0) complete(nseq - 1, hdrAt, bodyAt < 0 ? n : bodyAt, n);
  allSent = true;
  flush();
  finish();
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
else if (workerData === "r") readerThread();
else workerThread();
