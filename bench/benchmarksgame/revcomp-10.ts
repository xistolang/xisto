/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

reverse-complement, xisto #10: as #9 (each sequence read backwards from the file by threads, a block of output
lines at a time, into a ring of ~8 MB: memory holds a few blocks, not the sequence), with the newlines of a piece
moved to the output's places before the reverse instead of after it. When a sequence's length is not a multiple
of 60 (r symbols in its last line; 20 for the first and third of the benchmark's), a piece of the output starts r
symbols into an input line: #9 reversed it as read, so the reverse's two ends met the newlines at different places
and turned each line in two runs (r and 60 - r) with a search for the newline after each, and then moved r bytes
of every line. #10 moves the same r bytes of every line first, so the newlines are every 60 symbols from the
piece's start on both ends: one run a line. Same symbols, same pairs, same output.
Graviton2 (9 runs interleaved, the 250 MB input from a file, the output read through a pipe): 0,282 s, 10 MB
(#9 0,295; #1 0,288, 244 MB; -6 0,282, 188 MB; Rust 0,555, 123 MB). Xeon: 0,843 s (#9 0,842; Rust 1,090).
 */

import { fstatSync, readFileSync, readSync } from "node:fs";
import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";
import { byteMap, reverse } from "xisto:bytes";

const COMPLEMENT = byteMap("ACGTUMRWSYKVHDBN", "TGCAAKYWSRMBDHVN", { ignoreCase: true });
const LINE: int = 60; // symbols per line (the input's lines are assumed to be 60 long, the last one shorter)
const LINES: int = 16384; // output lines per block (~1 MB)
const PIECE: int = 8192; // lines turned at a time (~0,5 MB: reverse keeps a piece below 1 MB on its own thread)
const ROOM: int = 1 << 16; // for a header, written with its sequence's first block
const SLOT: int = ROOM + (LINE + 1) * LINES + 1; // bytes of a slot of the ring
const SLOTS: int = 8; // blocks being made or waiting to be written
const DEPTH: int = 3; // blocks handed to a worker at a time

let size: int = 0;
let whole = new Uint8Array(0); // stdin read whole, when it is not a file

// len bytes of the input from position pos into buf at off
function readAt(buf: Uint8Array, off: int, len: int, pos: int): void {
  if (whole.length > 0) {
    buf.set(whole.subarray(pos, pos + len), off);
    return;
  }
  let done: int = 0;
  while (done < len) {
    const got = readSync(0, buf, off + done, len - done, pos + done);
    if (got <= 0) throw new Error("stdin ended early");
    done += got;
  }
}

// where the input symbol i of a sequence is, from the start of its body
function posOf(i: int): int {
  return i + Math.trunc(i / LINE);
}

// output symbols [k0, k1) of a body at b of n symbols (r in its last line), turned into buf from off: the end
function turn(buf: Uint8Array, off: int, b: int, n: int, r: int, endsNl: boolean, k0: int, k1: int): int {
  let end: int = off;
  for (let q0: int = k0; q0 < k1; q0 += LINE * PIECE) {
    const q1: int = Math.min(k1, q0 + LINE * PIECE); // output symbols [q0, q1): input symbols [n - q1, n - q0)
    const p0: int = posOf(n - q1), p1: int = posOf(n - q0 - 1) + 1;
    const at: int = end;
    readAt(buf, at, p1 - p0, b + p0);
    end = at + (p1 - p0);
    if (q1 < n && r !== 0) {
      // the piece starts r symbols into an input line: each newline moves r places on (the r symbols after it one
      // back), so that the newlines are where the output's go, every LINE symbols from the piece's start, the last
      // at its end. The symbols keep their order, so the reverse pairs the same ones; and its two ends now meet
      // the newlines at the same places, a line at a time (before: runs of r and LINE - r)
      for (let e: int = at + LINE - r; e < end; e += LINE + 1) {
        for (let t: int = e; t < e + r; t++) buf[t] = buf[t + 1];
        buf[e + r] = 10;
      }
      reverse(buf, { map: COMPLEMENT, skip: "\n", from: at, to: end });
    } else {
      reverse(buf, { map: COMPLEMENT, skip: "\n", from: at, to: end });
      if (q1 < n || endsNl) {
        buf[end] = 10; // the newline after the piece's last line (the input's is past the piece)
        end++;
      }
    }
  }
  return end;
}

// a sequence [h, e) as the blocks of its output: [kind, h, hl, b, n, r, endsNl, k0, k1] each (kind 0: a block of
// the body, with the header first when hl > 0; kind 1: raw bytes [h, h + hl) as they are)
function plan(h: int, e: int, tasks: int[]): void {
  const head = new Uint8Array(4096);
  let hl: int = -1, have: int = 0;
  while (hl < 0 && h + have < e) {
    const n: int = Math.min(head.length, e - h - have);
    readAt(head, 0, n, h + have);
    const nl = head.subarray(0, n).indexOf(10);
    if (nl >= 0) hl = have + nl + 1;
    have += n;
  }
  const raw = (from: int, len: int): void => {
    for (let at: int = from; at < from + len; at += SLOT) tasks.push(1, at, Math.min(SLOT, from + len - at), 0, 0, 0, 0, 0, 0);
  };
  if (hl < 0) { raw(h, e - h); return; } // no body: the part is all header
  const b: int = h + hl, len: int = e - b;
  if (len === 0) { raw(h, hl); return; }
  readAt(head, 0, 1, e - 1);
  const endsNl: int = head[0] === 10 ? 1 : 0;
  const lines: int = endsNl === 1 ? Math.trunc((len + LINE) / (LINE + 1)) : Math.trunc(len / (LINE + 1)); // newlines
  const n: int = len - lines; // symbols
  const r: int = n % LINE; // in the last line (0: it is full)
  if (hl > ROOM) raw(h, hl); // (a header too long to go with the first block)
  for (let k0: int = 0; k0 < n; k0 += LINE * LINES) {
    tasks.push(0, h, k0 === 0 && hl <= ROOM ? hl : 0, b, n, r, endsNl, k0, Math.min(n, k0 + LINE * LINES));
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

// the whole input in this thread (a pipe): the blocks one after the other, written as they are made
function alone(): void {
  const buf = new Uint8Array(SLOT);
  const tasks: int[] = [];
  let start: int = 0;
  while (start < size) {
    let next: int = whole.indexOf(62, start + 1);
    if (next < 0) next = size;
    plan(start, next, tasks);
    start = next;
  }
  for (let i = 0; i < tasks.length; i += 9) process.stdout.write(buf.subarray(0, make(buf, 0, tasks, i)));
}

// the task at i of tasks made into buf at off: the end
function make(buf: Uint8Array, off: int, tasks: int[], i: int): int {
  if (tasks[i] === 1) {
    readAt(buf, off, tasks[i + 2], tasks[i + 1]);
    return off + tasks[i + 2];
  }
  const hl: int = tasks[i + 2];
  if (hl > 0) readAt(buf, off, hl, tasks[i + 1]);
  return turn(buf, off + hl, tasks[i + 3], tasks[i + 4], tasks[i + 5], tasks[i + 6] === 1, tasks[i + 7], tasks[i + 8]);
}

function mainThread(): void {
  const st = fstatSync(0);
  if (!(st.isFile() && st.size > 0)) {
    whole = readFileSync(0);
    size = whole.length;
    alone();
    return;
  }
  size = st.size;
  const ring = new Uint8Array(new SharedArrayBuffer(SLOT * SLOTS));
  const tasks: int[] = []; // planned, in output order
  let given: int = 0, written: int = 0; // tasks handed out; tasks written
  const slotOf: int[] = [], ends: int[] = []; // per task given: its slot; its end in the slot (-1: not done)
  const free: int[] = [];
  for (let s = 0; s < SLOTS; s++) free.push(s);
  const busy: int[] = []; // blocks handed to each computing worker and not back yet
  let scanned = false, last: int = 0; // all headers known; where the sequence being scanned starts
  const nmakers = Math.max(1, Math.min(availableParallelism() - 1, 16)); // (a core for the scanner and the writes)
  const makers: Worker[] = [];
  const give = (wk: Worker): boolean => {
    if (given * 9 >= tasks.length || free.length === 0) return false;
    const s = free.shift()!;
    const t = new Uint8Array(4 + 4 + 9 * 4);
    put32(t, 0, s);
    put32(t, 4, given);
    for (let k = 0; k < 9; k++) put32(t, 8 + 4 * k, tasks[given * 9 + k]);
    slotOf.push(s);
    ends.push(-1);
    given++;
    wk.postMessage(t);
    return true;
  };
  // blocks to the workers, in output order, each kept up to DEPTH blocks ahead (they go on while this thread writes)
  const dispatch = (): void => {
    for (;;) {
      let w: int = -1;
      for (let k = 0; k < makers.length; k++) if (busy[k] < DEPTH && (w < 0 || busy[k] < busy[w])) w = k;
      if (w < 0 || !give(makers[w])) break;
      busy[w]++;
    }
    if (scanned && written * 9 >= tasks.length) for (const m of makers) m.terminate();
  };
  for (let k = 0; k < nmakers; k++) {
    const wk = new Worker(__filename, { workerData: "make" });
    makers.push(wk);
    busy.push(0);
    const me: int = k;
    wk.on("message", (m: Uint8Array) => {
      ends[get32(m, 4)] = get32(m, 0);
      while (written < given && ends[written] >= 0) { // out in order
        const s = slotOf[written];
        process.stdout.write(ring.subarray(s * SLOT, s * SLOT + ends[written]));
        free.push(s);
        written++;
      }
      busy[me]--;
      dispatch();
    });
    wk.postMessage(ring); // the shared ring first, then the blocks
  }
  const scanner = new Worker(__filename, { workerData: "scan" });
  scanner.on("message", (m: Float64Array) => {
    const at: int = Math.trunc(m[0]); // the next header, or the end of the input
    plan(last, at, tasks);
    last = at;
    if (at >= size) {
      scanned = true;
      scanner.terminate();
    }
    dispatch();
  });
}

function scanThread(): void {
  size = fstatSync(0).size;
  const block = new Uint8Array(1 << 20);
  for (let pos: int = 0; pos < size;) {
    const n: int = Math.min(block.length, size - pos);
    readAt(block, 0, n, pos);
    const view = block.subarray(0, n);
    for (let at = view.indexOf(62, 0); at >= 0; at = view.indexOf(62, at + 1)) {
      if (pos + at === 0) continue; // the first header starts the first sequence
      const r = new Float64Array(1);
      r[0] = Number(pos + at);
      parentPort.postMessage(r);
    }
    pos += n;
  }
  const r = new Float64Array(1);
  r[0] = Number(size);
  parentPort.postMessage(r);
}

function makeThread(): void {
  size = fstatSync(0).size;
  let ring = new Uint8Array(0);
  const tasks: int[] = new Array<int>(9).fill(0);
  parentPort.on("message", (m: Uint8Array) => {
    if (ring.length === 0) { ring = m; return; } // the first message is the ring
    const s: int = get32(m, 0);
    for (let k = 0; k < 9; k++) tasks[k] = get32(m, 8 + 4 * k);
    const end: int = make(ring, s * SLOT, tasks, 0) - s * SLOT;
    const r = new Uint8Array(8);
    put32(r, 0, end);
    put32(r, 4, get32(m, 4));
    parentPort.postMessage(r);
  });
}

if (isMainThread) mainThread();
else if (workerData === "scan") scanThread();
else makeThread();
