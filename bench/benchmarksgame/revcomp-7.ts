/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

reverse-complement, xisto #7: #6's memory (one sequence at a time, always at the start of the shared buffer:
only the largest sequence's pages are ever resident) with #5's overlap back. A reader worker reads stdin into
the buffer; when a sequence is complete it hands it to the main thread, which splits it into ranges for the
computing workers and then writes it out in 1 MB chunks, telling the reader after each chunk how far the bytes are
free. The reader fills the next sequence in behind that write cursor, so reading the next sequence overlaps
writing this one. When stdin is not a file (a pipe), the main thread reads all of it first, as #5 does.
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
const WCHUNK: int = 1 << 20; // written at a time, the reader told after each

// a message to the main thread: a complete sequence (header start, body start, end), or the end of the input
function announce(s: int, hdr: int, bodyStart: int, end: int): void {
  const t = new Uint8Array(16);
  put32(t, 0, s);
  put32(t, 4, hdr);
  put32(t, 8, bodyStart);
  put32(t, 12, end);
  parentPort.postMessage(t);
}

const ALL: int = 0x7fffffff; // the whole buffer is free

class Reader {
  buf: Uint8Array;
  n: int;
  limit: int; // bytes at the front this thread may write
  have: int = 0; // bytes of the current sequence at the front
  scanned: int = 0;
  inHeader: boolean = false;
  open: boolean = false;
  eof: boolean = false;
  ended: boolean = false;
  bodyAt: int = -1;
  seq: int = 0;
  tail: Uint8Array = new Uint8Array(BLOCK); // read past the end of a sequence: the next one's start
  tailLen: int = -1; // -1: nothing kept aside
  constructor(buf: Uint8Array) {
    this.buf = buf;
    this.n = buf.length;
    this.limit = buf.length;
  }
  pump(): void {
    const b = this.buf;
    while (!this.ended) {
      if (this.tailLen >= 0) {
        if (this.limit < this.tailLen) return; // the front is not free yet
        b.set(this.tail.subarray(0, this.tailLen), 0);
        this.have = this.tailLen;
        this.tailLen = -1;
        this.scanned = 1; // the '>' that opens it is at 0
        this.inHeader = true;
        this.open = true;
        this.bodyAt = -1;
      }
      let done = false;
      while (this.scanned < this.have && !done) {
        const fresh = b.subarray(this.scanned, this.have);
        if (this.inHeader) {
          const nl = fresh.indexOf(10, 0);
          if (nl < 0) { this.scanned = this.have; break; }
          this.bodyAt = this.scanned + nl + 1;
          this.inHeader = false;
          this.scanned = this.bodyAt;
        } else {
          const rel = fresh.indexOf(62, 0);
          if (rel < 0) { this.scanned = this.have; break; }
          const gt = this.scanned + rel;
          if (this.open) {
            this.tail.set(b.subarray(gt, this.have), 0);
            this.tailLen = this.have - gt;
            announce(this.seq, 0, this.bodyAt < 0 ? gt : this.bodyAt, gt);
            this.seq++;
            this.limit = 0; // the front is the main thread's until it says otherwise
            done = true;
          } else {
            this.open = true;
            this.inHeader = true;
            this.scanned = gt + 1;
          }
        }
      }
      if (done) continue;
      if (this.eof) {
        if (this.open) announce(this.seq, 0, this.bodyAt < 0 ? this.have : this.bodyAt, this.have);
        announce(-1, 0, 0, 0);
        this.ended = true;
        return;
      }
      if (this.have >= this.n) { this.eof = true; continue; } // the buffer is the file's size: all read
      const room = Math.min(BLOCK, Math.min(this.limit, this.n) - this.have);
      if (room <= 0) return; // wait for the write cursor
      const got = readSync(0, b, this.have, room, null);
      if (got <= 0) this.eof = true;
      else this.have += got;
    }
  }
}

// the reader: stdin in blocks into the start of the shared memory, a sequence at a time; a sequence is
// announced when the next '>' (or the end) is found; the next one is read behind the main thread's write
// cursor (the free bytes it reports), its first bytes kept aside until the front is free
function readerThread(): void {
  const rs: Reader[] = [];
  parentPort.on("message", (m: Uint8Array) => {
    if (rs.length === 0) { rs.push(new Reader(m)); rs[0].pump(); return; }
    const r = rs[0];
    if (get32(m, 0) !== r.seq - 1) return; // about a sequence before the one now at the front: stale
    const v = get32(m, 4);
    r.limit = v === ALL ? r.n : v;
    r.pump();
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
  let reader: Worker | null = null;
  const tell = (s: int, v: int): void => { // sequence s's bytes before v are free (ALL: all of them)
    if (reader === null) return;
    const t = new Uint8Array(8);
    put32(t, 0, s);
    put32(t, 4, v);
    reader.postMessage(t);
  };
  const flush = (): void => {
    while (written < hdr.length && pending[written] === 0) {
      const end = bodyEnd[written];
      for (let at = hdr[written]; at < end; at += WCHUNK) {
        const to = Math.min(end, at + WCHUNK);
        process.stdout.write(buf.subarray(at, to));
        tell(written, to); // the bytes before to are free for the next sequence
      }
      tell(written, ALL);
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
    const rd = new Worker(__filename, { workerData: "r" });
    reader = rd;
    rd.on("message", (m: Uint8Array) => {
      const s = get32(m, 0);
      if (s < 0) { // the end
        allSent = true;
        rd.terminate();
        flush();
        finish();
        return;
      }
      complete(s, get32(m, 4), get32(m, 8), get32(m, 12));
      flush();
    });
    rd.postMessage(buf);
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
