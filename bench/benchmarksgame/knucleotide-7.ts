/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

k-nucleotide, xisto #7: as #6, with the sequence kept two bits per nucleotide, four to a byte (~30 MB for the
125 M nucleotides of the third sequence, not ~120 MB): the nucleotide j is (seq[j >> 2] >> 2 * (j & 3)) & 3. The
input is read in blocks of 1 MB, each block's nucleotides converted into a small array and packed from there.

k-nucleotide, xisto #6: as #5, with the frames of at most 8 nucleotides (4 ** 8 = 65536 keys) counted in a table
indexed by the key instead of a Map, and the two big frames in three partitions each, as countSubstrings of
xisto:collections does (knucleotide.ts on the Xeon: 2,31 s against 8,73 of #5: five Map passes over the whole
sequence for the small frames, and the big tables per partition fitting the caches).

k-nucleotide, xisto #5: as #4, with the input read and converted with indexOf for the headers and a
branch-free loop for the sequence (the byte-by-byte state machine was 9% of the CPU and all of it before the
workers start).

k-nucleotide, xisto #4: as #3 (sequence on a SharedArrayBuffer, frames sliced among the workers), with the next
key made as the fastest Rust program makes it: the first nucleotide out by keeping the key modulo 4 ** (k - 1),
read from a table of powers of four, then the next one in. The compiler knows the divisor is a power of two
within the table's range, so the modulo is a mask and the key cannot overflow: no checks in the loop.
 */

import { fstatSync, readFileSync, readSync } from "node:fs";
import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort } from "node:worker_threads";

const CODES: string[] = ["GGT", "GGTA", "GGTATT", "GGTATTTTAATT", "GGTATTTTAATTTATAGT"];
const KS: int[] = [18, 12, 6, 4, 3, 2, 1]; // the frames, the most keys first (answer KS.length - 1 - f)
const BIG: int = 2; // workers on the two frames with many keys at a time (M4: 2 and 3 tied on wall, 2 took the least CPU)

// 4 ** n for the frame lengths used (k <= 18)
const POW4: int[] = [1, 4, 16, 64, 256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216, 67108864, 268435456, 1073741824, 4294967296, 17179869184];

// counts of the k-mers of seq starting at lo..hi-1 (two bits per nucleotide, the first one highest). The packed
// nucleotide j is (seq[j >> 2] >> (6 - ((j & 3) << 1))) & 3; the counting loops take a byte at a time
function count(seq: Uint8Array, k: int, lo: int, hi: int): Map<int, int> {
  if (hi <= lo) return new Map<int, int>();
  return k <= 8 ? countFew(seq, k, lo, hi) : k <= 12 ? countWindow(seq, k, lo, hi) : countMany(seq, k, lo, hi);
}

// few possible keys: a table indexed by the key, then the ones counted into the Map. The bytes go through a 32-bit
// window w (the last four bytes, the newest lowest): the k-mers that end in the newest byte are w >> 6, w >> 4,
// w >> 2 and w, each masked to its 2k bits, with no key carried from one to the next
function countFew(seq: Uint8Array, k: int, lo: int, hi: int): Map<int, int> {
  const counts = new Map<int, int>();
  const mask = POW4[k] - 1;
  const tally: int[] = new Array<int>(POW4[k]).fill(0);
  const jfirst: int = lo + k - 1, end: int = hi + k - 1; // where the k-mers end
  const bfirst: int = jfirst >> 2, blast: int = (end - 1) >> 2; // the bytes they end in
  let w: int = 0;
  for (let b = lo >> 2; b < bfirst; b++) w = (w << 8) | seq[b];
  // the first byte and the last one: only the k-mers that end in [jfirst, end)
  w = (w << 8) | seq[bfirst];
  for (let r = 0; r < 4; r++) {
    const j: int = (bfirst << 2) + r;
    if (j >= jfirst && j < end) tally[(w >> (6 - (r << 1))) & mask]++;
  }
  for (let b = bfirst + 1; b < blast; b++) {
    w = (w << 8) | seq[b];
    tally[(w >> 6) & mask]++;
    tally[(w >> 4) & mask]++;
    tally[(w >> 2) & mask]++;
    tally[w & mask]++;
  }
  if (blast > bfirst) {
    w = (w << 8) | seq[blast];
    for (let r = 0; r < 4; r++) if ((blast << 2) + r < end) tally[(w >> (6 - (r << 1))) & mask]++;
  }
  for (let t = 0; t <= mask; t++) if (tally[t] > 0) counts.set(t, tally[t]);
  return counts;
}

// keys of up to 24 bits (k <= 12): the same window, the keys counted in the Map
function countWindow(seq: Uint8Array, k: int, lo: int, hi: int): Map<int, int> {
  const counts = new Map<int, int>();
  const mask = POW4[k] - 1;
  const jfirst: int = lo + k - 1, end: int = hi + k - 1; // where the k-mers end
  const bfirst: int = jfirst >> 2, blast: int = (end - 1) >> 2; // the bytes they end in
  let w: int = 0;
  for (let b = lo >> 2; b < bfirst; b++) w = (w << 8) | seq[b];
  w = (w << 8) | seq[bfirst];
  for (let r = 0; r < 4; r++) {
    const j: int = (bfirst << 2) + r;
    if (j >= jfirst && j < end) {
      const key: int = (w >> (6 - (r << 1))) & mask;
      if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
      else counts.set(key, 1);
    }
  }
  for (let b = bfirst + 1; b < blast; b++) {
    w = (w << 8) | seq[b];
    const k0: int = (w >> 6) & mask, k1: int = (w >> 4) & mask, k2: int = (w >> 2) & mask, k3: int = w & mask;
    if (counts.has(k0)) counts.set(k0, counts.get(k0)! + 1);
    else counts.set(k0, 1);
    if (counts.has(k1)) counts.set(k1, counts.get(k1)! + 1);
    else counts.set(k1, 1);
    if (counts.has(k2)) counts.set(k2, counts.get(k2)! + 1);
    else counts.set(k2, 1);
    if (counts.has(k3)) counts.set(k3, counts.get(k3)! + 1);
    else counts.set(k3, 1);
  }
  if (blast > bfirst) {
    w = (w << 8) | seq[blast];
    for (let r = 0; r < 4; r++) {
      if ((blast << 2) + r < end) {
        const key: int = (w >> (6 - (r << 1))) & mask;
        if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
        else counts.set(key, 1);
      }
    }
  }
  return counts;
}

// many possible keys (more bits than the window holds): the next k-mer is the key without its first nucleotide
// (the key modulo 4 ** (k - 1)) and the next one in
function countMany(seq: Uint8Array, k: int, lo: int, hi: int): Map<int, int> {
  const counts = new Map<int, int>();
  const top = POW4[k - 1]; // 4 ** (k - 1): what the key keeps of the k-mer before
  let key: int = 0;
  for (let i = lo; i < lo + k; i++) key = key * 4 + ((seq[i >> 2] >> (6 - ((i & 3) << 1))) & 3);
  counts.set(key, 1);
  const end = hi + k - 1;
  let j: int = lo + k;
  for (; j < end && (j & 3) !== 0; j++) {
    key = (key % top) * 4 + ((seq[j >> 2] >> (6 - ((j & 3) << 1))) & 3);
    if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
    else counts.set(key, 1);
  }
  const first: int = j >> 2, last: int = end >> 2;
  for (let b = first; b < last; b++) {
    const v: int = seq[b];
    key = (key % top) * 4 + ((v >> 6) & 3);
    if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
    else counts.set(key, 1);
    key = (key % top) * 4 + ((v >> 4) & 3);
    if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
    else counts.set(key, 1);
    key = (key % top) * 4 + ((v >> 2) & 3);
    if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
    else counts.set(key, 1);
    key = (key % top) * 4 + (v & 3);
    if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
    else counts.set(key, 1);
  }
  for (j = Math.max(j, last << 2); j < end; j++) {
    key = (key % top) * 4 + ((seq[j >> 2] >> (6 - ((j & 3) << 1))) & 3);
    if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
    else counts.set(key, 1);
  }
  return counts;
}

function keyOf(code: string): int {
  const b = new TextEncoder().encode(code);
  let key: int = 0;
  for (let i = 0; i < b.length; i++) key = key * 4 + ((b[i] >> 1) & 3);
  return key;
}

function nameOf(key: int, k: int): string {
  let s = "";
  let v = key;
  for (let i = 0; i < k; i++) {
    s = "ACTG".slice(v & 3, (v & 3) + 1) + s; // ((c >> 1) & 3: A 0, C 1, T 2, G 3)
    v = v >> 2;
  }
  return s;
}

function frequencies(counts: Map<int, int>, k: int, total: int): string {
  const pairs = Array.from(counts).sort((a: [int, int], b: [int, int]) => b[1] - a[1]);
  let out = "";
  for (const [key, c] of pairs) out += `${nameOf(key, k)} ${(100.0 * Number(c) / Number(total)).toFixed(3)}\n`;
  return out + "\n";
}

function specific(counts: Map<int, int>, code: string): string {
  const key = keyOf(code);
  return `${counts.has(key) ? counts.get(key)! : 0}\t${code}\n`;
}

// the third sequence, two bits per nucleotide and four to a byte, the first one in the highest bits (the
// nucleotide j at bits 6 - 2 * (j & 3) of byte j >> 2). From a file the input is read in blocks of 1 MB and packed as
// it comes, straight into the shared memory (only the packed sequence is ever held: ~30 MB, not the 254 MB of the
// input); from a pipe it is read whole. seqLength: how many nucleotides it holds.
let seqLength: int = 0;
let npacked: int = 0; // whole bytes of seq written
let carry: int = 0; // the nucleotides of a byte not yet whole (0 to 3 of them, the first highest)
let npending: int = 0;
// the nucleotides of input[from..lim-1] (a c g t, A C G T: (c >> 1) & 3 is 0 1 3 2), line by line (the newlines
// found with indexOf), packed into seq
function addLines(seq: Uint8Array, input: Uint8Array, from: int, lim: int): void {
  let t: int = from;
  while (t < lim) {
    let e: int = input.indexOf(10, t);
    if (e < 0 || e > lim) e = lim;
    seqLength += e - t;
    while (npending > 0 && t < e) { // a byte begun before (a block's cut, a line not a multiple of 4): filled first
      carry = (carry << 2) | ((input[t] >> 1) & 3);
      npending++;
      t++;
      if (npending === 4) { seq[npacked] = carry; npacked++; carry = 0; npending = 0; }
    }
    const groups: int = (e - t) >> 2; // then whole bytes straight from the line
    const o: int = npacked;
    for (let g = 0; g < groups; g++) {
      const c: int = t + (g << 2);
      seq[o + g] = (((input[c] >> 1) & 3) << 6) | (((input[c + 1] >> 1) & 3) << 4) | (((input[c + 2] >> 1) & 3) << 2) | ((input[c + 3] >> 1) & 3);
    }
    npacked += groups;
    for (t += groups << 2; t < e; t++) { // and what is left, pending
      carry = (carry << 2) | ((input[t] >> 1) & 3);
      npending++;
    }
    t = e + 1;
  }
}
// the last byte, when it is not whole
function packRest(seq: Uint8Array): void {
  if (npending > 0) { seq[npacked] = carry << ((4 - npending) << 1); npacked++; }
  npending = 0;
}

// len bytes of stdin (a file) from position pos into buf
function readAt(buf: Uint8Array, len: int, pos: int): void {
  let done: int = 0;
  while (done < len) {
    const got = readSync(0, buf, done, len - done, pos + done);
    if (got <= 0) throw new Error("stdin ended early");
    done += got;
  }
}

// where the body of the third sequence starts in the file (the line after the one starting with ">TH"), or size
function findThree(size: int): int {
  const block = new Uint8Array(1 << 20);
  const three = new Uint8Array(3);
  let pos: int = 0;
  let after: int = -1; // a ">TH" line found: its newline is looked for from here
  while (pos < size) {
    const got: int = Math.min(block.length, size - pos);
    readAt(block, got, pos);
    const view = block.subarray(0, got);
    let i: int = 0;
    while (i < got) {
      if (after >= 0) {
        const nl = view.indexOf(10, i);
        if (nl >= 0) return pos + nl + 1;
        i = got;
      } else {
        const p = view.indexOf(62, i);
        if (p < 0) { i = got; continue; }
        const at: int = pos + p;
        let lineStart = at === 0;
        if (!lineStart) {
          if (p > 0) lineStart = block[p - 1] === 10;
          else { readAt(three, 1, at - 1); lineStart = three[0] === 10; }
        }
        if (lineStart && at + 2 < size) {
          readAt(three, 3, at);
          if (three[1] === 84 && three[2] === 72) after = at + 3;
        }
        i = p + 1;
        if (after >= 0) i = Math.max(i, after - pos);
      }
    }
    pos += got;
  }
  return size;
}

// the sequence from the body at b on, packed by this thread: line by line, to a '>' or the end of the file
function packFrom(seq: Uint8Array, b: int, size: int): void {
  seqLength = 0; npacked = 0; carry = 0; npending = 0;
  const block = new Uint8Array(1 << 20);
  let pos: int = b;
  while (pos < size) {
    const got: int = Math.min(block.length, size - pos);
    readAt(block, got, pos);
    const view = block.subarray(0, got);
    const stop = view.indexOf(62);
    addLines(seq, view, 0, stop < 0 ? got : stop);
    if (stop >= 0) break;
    pos += got;
  }
  packRest(seq);
}

// lines l0..l1-1 of the body at b, each 60 symbols and a newline, packed into seq by a worker (15 bytes a line);
// false when a line is not that (a newline elsewhere, a '>': every symbol byte has bit 6, a newline and '>' do not)
function packLines(seq: Uint8Array, b: int, l0: int, l1: int): boolean {
  const per: int = 16384;
  const buf = new Uint8Array(61 * per);
  for (let l: int = l0; l < l1; l += per) {
    const m: int = Math.min(per, l1 - l);
    readAt(buf, 61 * m, b + 61 * l);
    let all: int = 64;
    const o: int = 15 * l;
    for (let j = 0; j < m; j++) {
      if (buf[61 * j + 60] !== 10) return false;
      const c0: int = 61 * j, d: int = o + 15 * j;
      for (let g = 0; g < 15; g++) {
        const c: int = c0 + (g << 2);
        const x0: int = buf[c], x1: int = buf[c + 1], x2: int = buf[c + 2], x3: int = buf[c + 3];
        all = all & x0 & x1 & x2 & x3;
        seq[d + g] = (((x0 >> 1) & 3) << 6) | (((x1 >> 1) & 3) << 4) | (((x2 >> 1) & 3) << 2) | ((x3 >> 1) & 3);
      }
    }
    if ((all & 64) === 0) return false;
  }
  return true;
}

function sequenceWhole(): Uint8Array {
  const input = readFileSync(0);
  const n: int = input.length;
  let at: int = 0;
  // the line after ">THREE"
  while (at < n) {
    const nl = input.indexOf(10, at);
    const end: int = nl < 0 ? n : nl;
    if (end - at >= 6 && input[at] === 62 && input[at + 1] === 84 && input[at + 2] === 72) { at = end + 1; break; }
    at = end + 1;
  }
  const seq = new Uint8Array(new SharedArrayBuffer(((n - at) >> 2) + 1));
  const stop = input.indexOf(62, at);
  const lim: int = stop < 0 ? n : stop;
  addLines(seq, input, at, lim);
  packRest(seq);
  return seq.subarray(0, npacked);
}

function mainThread(): void {
  // From a file, the third sequence is packed by the workers, each its share of the lines, read at their places
  // (only the packed sequence is ever held: ~30 MB); the last, short line here. If a line is not 60 symbols and a
  // newline, this thread packs it all instead, line by line. From a pipe it is read whole first.
  const st = fstatSync(0);
  const file = st.isFile() && st.size > 0; // (a pipe's size is what it holds at the moment)
  const size: int = file ? st.size : 0;
  const body: int = file ? findThree(size) : 0;
  const full: int = file ? Math.trunc((size - body) / 61) : 0; // whole lines (61 bytes) the workers may pack
  const seq = file ? new Uint8Array(new SharedArrayBuffer(((size - body) >> 2) + 2)) : sequenceWhole();
  let n: int = seqLength;
  const nworkers = Math.min(availableParallelism(), 32);
  // The frames with many keys (18 and 12: ~140 K each, a table of ~4 MB per partition) are counted by at most
  // BIG workers at a time, so that their tables share the caches with few others (on an M4, ten of them at once
  // ran 3x slower per symbol than two); the small frames fill the other workers meanwhile. A worker with nothing
  // left in its own queue takes from the other one.
  const nbig = Math.min(BIG, nworkers);
  const bigK: int[] = [];
  const bigP: int[] = [];
  const smallK: int[] = [];
  const smallP: int[] = [];
  const parts: int[] = new Array<int>(KS.length).fill(0); // partitions of each frame
  for (let f = 0; f < KS.length; f++) {
    const k = KS[f];
    const big = k > 6;
    parts[f] = big ? (nworkers > 2 ? nbig * 2 : 3) : nworkers; // (with two workers, three partitions: smaller tables, as countSubstrings)
    for (let p = 0; p < parts[f]; p++) {
      if (big) { bigK.push(k); bigP.push(p); } else { smallK.push(k); smallP.push(p); }
    }
  }
  const ntasks = bigK.length + smallK.length;
  const merged: Map<int, int>[] = [];
  const pending: int[] = []; // partitions of each frame still to come
  for (let f = 0; f < KS.length; f++) { merged.push(new Map<int, int>()); pending.push(parts[f]); }
  const answers: string[] = new Array<string>(2 + CODES.length).fill("");
  let nextBig = 0, nextSmall = 0, left = ntasks;
  let packing: int = 0, packed = true; // packing tasks out; all of them succeeded
  const starts: (() => boolean)[] = [];
  // the packing done: the rest of the sequence here (or all of it, when a worker found a line it cannot pack),
  // then the counting tasks
  const counting = (): void => {
    if (packed) {
      seqLength = 60 * full; npacked = 15 * full; carry = 0; npending = 0;
      const rest: int = size - body - 61 * full;
      if (rest > 0) {
        const tail = new Uint8Array(rest);
        readAt(tail, rest, body + 61 * full);
        const stop = tail.indexOf(62);
        addLines(seq, tail, 0, stop < 0 ? rest : stop);
      }
      packRest(seq);
    } else packFrom(seq, body, size);
    n = seqLength;
    for (const s of starts) s();
  };
  for (let w = 0; w < nworkers; w++) {
    const wk = new Worker(__filename, { workerData: "" });
    let myK: int = -1;
    const give = (): boolean => {
      let k: int = -1, p: int = -1;
      const bigFirst = w < nbig;
      if ((bigFirst || nextSmall >= smallK.length) && nextBig < bigK.length) { k = bigK[nextBig]; p = bigP[nextBig]; nextBig++; }
      else if (nextSmall < smallK.length) { k = smallK[nextSmall]; p = smallP[nextSmall]; nextSmall++; }
      else if (nextBig < bigK.length) { k = bigK[nextBig]; p = bigP[nextBig]; nextBig++; }
      else return false;
      myK = k;
      const t = new Uint8Array(7);
      t[0] = k;
      t[1] = p;
      t[2] = parts[KS.indexOf(k)];
      t[3] = n & 0xff; // the nucleotides in the sequence, in four bytes
      t[4] = (n >> 8) & 0xff;
      t[5] = (n >> 16) & 0xff;
      t[6] = (n >> 24) & 0xff;
      wk.postMessage(t);
      return true;
    };
    wk.on("message", (r: Float64Array) => {
      if (r.length === 1) { // a packing task done (1: packed)
        if (r[0] !== 1) packed = false;
        packing--;
        if (packing === 0) counting();
        return;
      }
      const k = myK;
      const f = KS.indexOf(k);
      const m = merged[f];
      for (let i = 0; i < r.length; i += 2) {
        const key = Math.trunc(r[i]);
        const c = Math.trunc(r[i + 1]);
        if (m.has(key)) m.set(key, m.get(key)! + c);
        else m.set(key, c);
      }
      pending[f]--;
      if (pending[f] === 0) {
        const a = KS.length - 1 - f;
        answers[a] = k <= 2 ? frequencies(m, k, n + 1 - k) : specific(m, CODES[a - 2]);
        m.clear();
      }
      left--;
      if (!give()) wk.terminate();
      if (left === 0) for (const s of answers) process.stdout.write(s);
    });
    wk.postMessage(seq); // the sequence first (shared memory), then one task at a time
    starts.push((): boolean => give());
    if (file && full > 0) { // its share of the lines to pack: [0, l0, l1, body] (four bytes each)
      const t = new Uint8Array(13);
      put32(t, 1, Math.trunc(Number(full) * Number(w) / Number(nworkers)));
      put32(t, 5, Math.trunc(Number(full) * Number(w + 1) / Number(nworkers)));
      put32(t, 9, body);
      packing++;
      wk.postMessage(t);
    }
  }
  if (packing === 0) {
    if (file) counting();
    else for (const s of starts) s();
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

function workerThread(): void {
  let seq: Uint8Array | null = null;
  parentPort.on("message", (m: Uint8Array) => {
    if (seq === null) { seq = m; return; } // the first message is the sequence
    if (m[0] === 0) { // lines to pack
      const ok = packLines(seq, get32(m, 9), get32(m, 1), get32(m, 5));
      const r = new Float64Array(1);
      r[0] = ok ? 1 : 0;
      parentPort.postMessage(r);
      return;
    }
    const k: int = m[0];
    const p: int = m[1];
    const parts: int = m[2];
    const n: int = m[3] | (m[4] << 8) | (m[5] << 16) | (m[6] << 24);
    const size: int = n + 1 - k; // k-mers in the sequence
    const lo: int = size > 0 ? Math.trunc(Number(size) * Number(p) / Number(parts)) : 0;
    const hi: int = size > 0 ? Math.trunc(Number(size) * Number(p + 1) / Number(parts)) : 0;
    const counts = count(seq, k, lo, hi);
    const out = new Float64Array(counts.size * 2);
    let j: int = 0;
    for (const [key, c] of counts) { out[j] = Number(key); out[j + 1] = Number(c); j += 2; }
    parentPort.postMessage(out);
  });
}

if (isMainThread) mainThread(); else workerThread();
