/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

k-nucleotide, xisto #2: the program written for xisto rather than translated from JS. The sequence is read as
bytes and each nucleotide becomes two bits, so a k-mer of up to 18 is an int key of a Map<int, int> (as the
fastest Rust programs pack it); each frame (1, 2 and the five sequences asked for) is counted by a worker of a
pool (node:worker_threads), the sequence handed over as bytes.
 */

import { readFileSync } from "node:fs";
import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort } from "node:worker_threads";

const CODES: string[] = ["GGT", "GGTA", "GGTATT", "GGTATTTTAATT", "GGTATTTTAATTTATAGT"];

// counts of the k-mers of seq (two bits per nucleotide, the first one highest)
function count(seq: Uint8Array, k: int): Map<int, int> {
  const counts = new Map<int, int>();
  let top: int = 1; // 4 ** (k - 1): what the key keeps of the k-mer before
  for (let i = 0; i < k - 1; i++) top *= 4;
  const size = seq.length + 1 - k;
  if (size <= 0) return counts;
  let key: int = 0;
  for (let i = 0; i < k; i++) key = key * 4 + seq[i];
  for (let i = 0; ; i++) {
    if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
    else counts.set(key, 1);
    if (i + 1 >= size) break;
    // the next k-mer: the first nucleotide out (a multiplication, not a division), the next one in
    key = (key - seq[i] * top) * 4 + seq[i + k];
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

function frequencies(seq: Uint8Array, k: int): string {
  const counts = count(seq, k);
  const total = seq.length + 1 - k;
  const pairs = Array.from(counts).sort((a: [int, int], b: [int, int]) => b[1] - a[1]);
  let out = "";
  for (const [key, c] of pairs) out += `${nameOf(key, k)} ${(100.0 * Number(c) / Number(total)).toFixed(3)}\n`;
  return out + "\n";
}

function specific(seq: Uint8Array, code: string): string {
  const counts = count(seq, code.length);
  const key = keyOf(code);
  return `${counts.has(key) ? counts.get(key)! : 0}\t${code}\n`;
}

// the task's answer: 0 and 1 are the frequencies of 1 and 2, 2.. the specific codes
function answer(seq: Uint8Array, task: int): string {
  return task < 2 ? frequencies(seq, task + 1) : specific(seq, CODES[task - 2]);
}

// the third sequence, two bits per nucleotide (one per byte)
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
  const seq = new Uint8Array(n - at);
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
  const tasks = 2 + CODES.length;
  const answers: string[] = new Array<string>(tasks).fill("");
  const order: int[] = [6, 5, 4, 3, 2, 1, 0]; // the longest k-mers (the most keys) first
  let next = 0, left = tasks;
  const nworkers = Math.min(availableParallelism(), tasks);
  for (let w = 0; w < nworkers; w++) {
    const wk = new Worker(__filename, { workerData: "" });
    let mine: int = -1;
    wk.on("message", (m: string) => {
      answers[mine] = m;
      left--;
      if (next < tasks) {
        mine = order[next];
        next++;
        const t = new Uint8Array(1); // (the worker has the sequence already)
        t[0] = mine;
        wk.postMessage(t);
      } else wk.terminate();
      if (left === 0) for (const a of answers) process.stdout.write(a);
    });
    mine = order[next];
    next++;
    wk.postMessage(seq); // the sequence first, then the task (a byte)
    const t = new Uint8Array(1);
    t[0] = mine;
    wk.postMessage(t);
  }
}

function workerThread(): void {
  let seq = new Uint8Array(0);
  parentPort.on("message", (m: Uint8Array) => {
    if (m.length > 1) seq = m; // the sequence; a task is a single byte
    else parentPort.postMessage(answer(seq, m[0]));
  });
}

if (isMainThread) mainThread(); else workerThread();
