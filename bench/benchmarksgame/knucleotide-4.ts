/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

k-nucleotide, xisto #4: as #3 (sequence on a SharedArrayBuffer, frames sliced among the workers), with the next
key made as the fastest Rust program makes it: the first nucleotide out by keeping the key modulo 4 ** (k - 1),
read from a table of powers of four, then the next one in. The compiler knows the divisor is a power of two
within the table's range, so the modulo is a mask and the key cannot overflow: no checks in the loop.
 */

import { readFileSync } from "node:fs";
import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort } from "node:worker_threads";

const CODES: string[] = ["GGT", "GGTA", "GGTATT", "GGTATTTTAATT", "GGTATTTTAATTTATAGT"];
const KS: int[] = [18, 12, 6, 4, 3, 2, 1]; // the frames, the most keys first (answer KS.length - 1 - f)
const BIG: int = 2; // workers on the two frames with many keys at a time (M4: 2 and 3 tied on wall, 2 took the least CPU)

// 4 ** n for the frame lengths used (k <= 18)
const POW4: int[] = [1, 4, 16, 64, 256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216, 67108864, 268435456, 1073741824, 4294967296, 17179869184];

// counts of the k-mers of seq starting at lo..hi-1 (two bits per nucleotide, the first one highest)
function count(seq: Uint8Array, k: int, lo: int, hi: int): Map<int, int> {
  const counts = new Map<int, int>();
  if (hi <= lo) return counts;
  const top = POW4[k - 1]; // 4 ** (k - 1): what the key keeps of the k-mer before
  let key: int = 0;
  for (let i = lo; i < lo + k; i++) key = key * 4 + seq[i];
  for (let i = lo; ; i++) {
    if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
    else counts.set(key, 1);
    if (i + 1 >= hi) break;
    // the next k-mer: the first nucleotide out (the key modulo 4 ** (k - 1)), the next one in
    key = (key % top) * 4 + seq[i + k];
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

// the third sequence, two bits per nucleotide (one per byte), on shared memory
function sequence(): Uint8Array {
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
  const seq = new Uint8Array(new SharedArrayBuffer(n - at));
  let len: int = 0;
  for (let i = at; i < n; i++) {
    const c = input[i];
    if (c === 62) break;
    if (c !== 10) { seq[len] = (c >> 1) & 3; len++; } // a c g t, A C G T: 0 1 3 2
  }
  return seq.subarray(0, len);
}

function mainThread(): void {
  const seq = sequence();
  const n: int = seq.length;
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
    parts[f] = big ? nbig * 2 : nworkers;
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
      const t = new Uint8Array(3);
      t[0] = k;
      t[1] = p;
      t[2] = parts[KS.indexOf(k)];
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
    const size: int = seq.length + 1 - k; // k-mers in the sequence
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
