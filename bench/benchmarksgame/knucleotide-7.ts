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
let pending: int = 0; // the nucleotides of a byte not yet whole (0 to 3 of them, the first highest)
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
      pending = (pending << 2) | ((input[t] >> 1) & 3);
      npending++;
      t++;
      if (npending === 4) { seq[npacked] = pending; npacked++; pending = 0; npending = 0; }
    }
    const groups: int = (e - t) >> 2; // then whole bytes straight from the line
    const o: int = npacked;
    for (let g = 0; g < groups; g++) {
      const c: int = t + (g << 2);
      seq[o + g] = (((input[c] >> 1) & 3) << 6) | (((input[c + 1] >> 1) & 3) << 4) | (((input[c + 2] >> 1) & 3) << 2) | ((input[c + 3] >> 1) & 3);
    }
    npacked += groups;
    for (t += groups << 2; t < e; t++) { // and what is left, pending
      pending = (pending << 2) | ((input[t] >> 1) & 3);
      npending++;
    }
    t = e + 1;
  }
}
// the last byte, when it is not whole
function packRest(seq: Uint8Array): void {
  if (npending > 0) { seq[npacked] = pending << ((4 - npending) << 1); npacked++; }
  npending = 0;
}

function sequence(): Uint8Array {
  const st = fstatSync(0);
  if (!st.isFile() || st.size <= 0) return sequenceWhole(); // (a pipe's size is what it holds at the moment)
  const size: int = st.size;
  const seq = new Uint8Array(new SharedArrayBuffer((size >> 2) + 1));
  const block = new Uint8Array(1 << 20);
  let state: int = 0; // 0: looking for the line ">TH..."; 1: the rest of that line; 2: in the sequence; 3: done
  let lineStart = true; // the byte before the block's first one ended a line
  let matched: int = 0; // of ">TH" at the start of a line
  while (state < 3) {
    const got = readSync(0, block, 0, block.length, null);
    if (got <= 0) break;
    const view = block.subarray(0, got);
    let i: int = 0;
    while (i < got && state < 3) {
      if (state === 2) {
        // the sequence: every byte but the newlines, two bits each; a '>' ends it. Line by line: the newline
        // found with indexOf, whole groups of four packed by a counted loop
        const stop = view.indexOf(62, i);
        const lim: int = stop < 0 ? got : stop;
        addLines(seq, view, i, lim);
        if (stop >= 0) state = 3;
        i = got;
      } else if (state === 1) {
        const nl = view.indexOf(10, i);
        if (nl < 0) i = got;
        else { state = 2; i = nl + 1; }
      } else if (matched === 0) {
        // the next '>' (the headers are the only ones), then ">TH" at a line start, byte by byte
        const p = view.indexOf(62, i);
        if (p < 0) { lineStart = block[got - 1] === 10; i = got; }
        else { if (p > i) lineStart = block[p - 1] === 10; if (lineStart) matched = 1; lineStart = false; i = p + 1; }
      } else {
        const c = block[i];
        if (matched === 1 && c === 84) matched = 2;
        else if (matched === 2 && c === 72) { matched = 0; state = 1; }
        else matched = 0;
        lineStart = c === 10;
        i++;
      }
    }
  }
  packRest(seq);
  return seq.subarray(0, npacked);
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
  const seq = sequence();
  const n: int = seqLength;
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
    give();
  }
}

function workerThread(): void {
  let seq: Uint8Array | null = null;
  parentPort.on("message", (m: Uint8Array) => {
    if (seq === null) { seq = m; return; } // the first message is the sequence
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
