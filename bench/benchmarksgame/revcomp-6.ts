/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

reverse-complement, xisto #6: as #5 (blocks of stdin read straight into a SharedArrayBuffer, each complete
sequence's symbol pairs split into one range per worker, written out in order), but one sequence at a time and
always at the start of the buffer, as the Rust program does: the shared memory is as large as the file, but
only the pages the largest sequence touches are ever resident. A sequence is read, computed and written; the
bytes already read past it (the start of the next one) move to the front, and the next one is read after
them. The peak memory is the largest sequence, not the whole input. When stdin is not a file (a pipe), the
main thread reads all of it first, as #5 does.
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
  let onWritten = (): void => {}; // the streamed path reuses the buffer once a sequence is out
  const flush = (): void => {
    while (written < hdr.length && pending[written] === 0) {
      process.stdout.write(buf.subarray(hdr[written], bodyEnd[written]));
      written++;
      onWritten();
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
    // one sequence at a time, from the start of buf: read until the next '>' (or the end), compute, write,
    // move what was read past it to the front, go on
    let have: int = 0; // bytes valid at the front of buf
    let scanned: int = 0;
    let inHeader = false, open = false, eof = false;
    let bodyAt: int = -1, seq: int = 0;
    let cut: int = 0; // where the sequence being computed ends (the next one starts there)
    const step = (): void => {
      while (true) {
        while (scanned < have) {
          const fresh = buf.subarray(scanned, have);
          if (inHeader) {
            const nl = fresh.indexOf(10, 0);
            if (nl < 0) { scanned = have; break; }
            bodyAt = scanned + nl + 1;
            inHeader = false;
            scanned = bodyAt;
          } else {
            const rel = fresh.indexOf(62, 0);
            if (rel < 0) { scanned = have; break; }
            const gt = scanned + rel;
            if (open) { // the sequence before it is complete
              cut = gt;
              complete(seq, 0, bodyAt < 0 ? gt : bodyAt, gt);
              seq++;
              flush();
              return; // back when its ranges are in (onDone)
            }
            open = true;
            inHeader = true;
            scanned = gt + 1;
          }
        }
        if (eof) {
          if (open) {
            cut = have;
            complete(seq, 0, bodyAt < 0 ? have : bodyAt, have);
            seq++;
            open = false;
          }
          allSent = true;
          flush();
          finish();
          return;
        }
        const got = have < n ? readSync(0, buf, have, Math.min(BLOCK, n - have), null) : 0;
        if (got <= 0) eof = true;
        else have += got;
      }
    };
    // a sequence was written (flush): its bytes are free; what follows it moves to the front
    onWritten = (): void => {
      if (cut < have) buf.set(buf.subarray(cut, have), 0);
      have -= cut;
      scanned = 1; // the '>' that opened the next sequence is at 0
      inHeader = true;
      bodyAt = -1;
      cut = 0;
      if (!allSent) step();
    };
    step();
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
else workerThread();
