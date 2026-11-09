/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

mandelbrot, xisto #5: as #4, which is as #3 (chunks of 64 rows: four times fewer messages) and the image on a SharedArrayBuffer the
workers write into directly; a chunk comes back as its number only (no copy, no image.set); and the real part of
every pixel column computed once per worker into a Float64Array (the same expression, so the same doubles),
instead of eight divisions per byte of the row (Xeon: -7% of CPU). The rest as #3 (chunks of rows to a pool of workers, eight pixels at a time as eight independent
sets of variables, the escape tested every five iterations), with the test shaped like the arithmetic: the
eight sums first, then the comparisons reduced in pairs, (0,1) (2,3) (4,5) (6,7), the same pairing the C
compiler uses for the vector arithmetic, so no lane has to move between vectors for the test. And a byte is
only tested for escape when the previous byte of the row was all outside the set (0): inside the set nothing
escapes, so the test would only cost (the 50 iterations run either way, the bits come out the same).
The eight pixels of a byte in a function of their own, as the Rust #4 does: when all eight are outside at a test it
returns 0 at once (outside stays outside), instead of breaking out to run the last iteration and pack eight bits
that come out 0 anyway; the first iteration from zero is z = c exactly, and the test every 4 iterations, not 5
(Xeon: 4,02 -> 3,79 u of CPU; Rust 3,55).
 */

import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

const ROWS: int = 64; // rows per chunk

// the byte of eight pixels: one bit per pixel, 1 = inside the set after 50 iterations
// (prev is the byte before it in the row: 0 = all eight were outside, so these may escape too)
function byte8(r0: number, r1: number, r2: number, r3: number, r4: number, r5: number, r6: number, r7: number,
               ci: number, prev: int): int {
  // the first iteration from zero gives z = c exactly
  let zr0 = r0, zr1 = r1, zr2 = r2, zr3 = r3, zr4 = r4, zr5 = r5, zr6 = r6, zr7 = r7;
  let zi0 = ci, zi1 = ci, zi2 = ci, zi3 = ci, zi4 = ci, zi5 = ci, zi6 = ci, zi7 = ci;
  let tr0 = r0 * r0, tr1 = r1 * r1, tr2 = r2 * r2, tr3 = r3 * r3, tr4 = r4 * r4, tr5 = r5 * r5, tr6 = r6 * r6, tr7 = r7 * r7;
  const cc = ci * ci;
  let ti0 = cc, ti1 = cc, ti2 = cc, ti3 = cc, ti4 = cc, ti5 = cc, ti6 = cc, ti7 = cc;
  for (let i = 0; i < 12; i++) {
    for (let k = 0; k < 4; k++) {
      zi0 = 2 * zr0 * zi0 + ci; zr0 = tr0 - ti0 + r0; tr0 = zr0 * zr0; ti0 = zi0 * zi0;
      zi1 = 2 * zr1 * zi1 + ci; zr1 = tr1 - ti1 + r1; tr1 = zr1 * zr1; ti1 = zi1 * zi1;
      zi2 = 2 * zr2 * zi2 + ci; zr2 = tr2 - ti2 + r2; tr2 = zr2 * zr2; ti2 = zi2 * zi2;
      zi3 = 2 * zr3 * zi3 + ci; zr3 = tr3 - ti3 + r3; tr3 = zr3 * zr3; ti3 = zi3 * zi3;
      zi4 = 2 * zr4 * zi4 + ci; zr4 = tr4 - ti4 + r4; tr4 = zr4 * zr4; ti4 = zi4 * zi4;
      zi5 = 2 * zr5 * zi5 + ci; zr5 = tr5 - ti5 + r5; tr5 = zr5 * zr5; ti5 = zi5 * zi5;
      zi6 = 2 * zr6 * zi6 + ci; zr6 = tr6 - ti6 + r6; tr6 = zr6 * zr6; ti6 = zi6 * zi6;
      zi7 = 2 * zr7 * zi7 + ci; zr7 = tr7 - ti7 + r7; tr7 = zr7 * zr7; ti7 = zi7 * zi7;
    }
    if (prev === 0) {
      const m0 = tr0 + ti0, m1 = tr1 + ti1, m2 = tr2 + ti2, m3 = tr3 + ti3;
      const m4 = tr4 + ti4, m5 = tr5 + ti5, m6 = tr6 + ti6, m7 = tr7 + ti7;
      const lo = (!(m0 <= 4) && !(m1 <= 4)) && (!(m2 <= 4) && !(m3 <= 4));
      const hi = (!(m4 <= 4) && !(m5 <= 4)) && (!(m6 <= 4) && !(m7 <= 4));
      if (lo && hi) return 0; // outside stays outside
    }
  }
  zi0 = 2 * zr0 * zi0 + ci; zr0 = tr0 - ti0 + r0; tr0 = zr0 * zr0; ti0 = zi0 * zi0;
  zi1 = 2 * zr1 * zi1 + ci; zr1 = tr1 - ti1 + r1; tr1 = zr1 * zr1; ti1 = zi1 * zi1;
  zi2 = 2 * zr2 * zi2 + ci; zr2 = tr2 - ti2 + r2; tr2 = zr2 * zr2; ti2 = zi2 * zi2;
  zi3 = 2 * zr3 * zi3 + ci; zr3 = tr3 - ti3 + r3; tr3 = zr3 * zr3; ti3 = zi3 * zi3;
  zi4 = 2 * zr4 * zi4 + ci; zr4 = tr4 - ti4 + r4; tr4 = zr4 * zr4; ti4 = zi4 * zi4;
  zi5 = 2 * zr5 * zi5 + ci; zr5 = tr5 - ti5 + r5; tr5 = zr5 * zr5; ti5 = zi5 * zi5;
  zi6 = 2 * zr6 * zi6 + ci; zr6 = tr6 - ti6 + r6; tr6 = zr6 * zr6; ti6 = zi6 * zi6;
  zi7 = 2 * zr7 * zi7 + ci; zr7 = tr7 - ti7 + r7; tr7 = zr7 * zr7; ti7 = zi7 * zi7;
  let b = 0;
  if (tr0 + ti0 <= 4) b |= 128;
  if (tr1 + ti1 <= 4) b |= 64;
  if (tr2 + ti2 <= 4) b |= 32;
  if (tr3 + ti3 <= 4) b |= 16;
  if (tr4 + ti4 <= 4) b |= 8;
  if (tr5 + ti5 <= 4) b |= 4;
  if (tr6 + ti6 <= 4) b |= 2;
  if (tr7 + ti7 <= 4) b |= 1;
  return b;
}

// the bits of one row, eight pixels per byte, into out from at
function row(y: int, w: int, h: int, out: Uint8Array, at: int, cr: Float64Array): void {
  const ci = 2 * y / h - 1;
  let prev = 0;
  for (let x = 0; x < w; x += 8) {
    // each pixel's real part exactly as the original computes it (c0 + k * step would round differently)
    let b = byte8(cr[x], cr[x + 1], cr[x + 2], cr[x + 3], cr[x + 4], cr[x + 5], cr[x + 6], cr[x + 7], ci, prev);
    const past = x + 8 - w; // pixels beyond the row's end (its last byte): zero bits, as the original pads
    if (past > 0) b &= (0xff << past) & 0xff;
    out[at + (x >> 3)] = b;
    prev = b;
  }
}

function mainThread(n: int): void {
  const w = n, h = n;
  const bpr = (w + 7) >> 3;
  const image = new Uint8Array(new SharedArrayBuffer(bpr * h));
  const chunks = Math.trunc((h + ROWS - 1) / ROWS);
  let next = 0, left = chunks;
  const nworkers = Math.max(1, Math.min(availableParallelism(), chunks));
  for (let k = 0; k < nworkers; k++) {
    const wk = new Worker(__filename, { workerData: `${n}` });
    wk.on("message", (m: Uint8Array) => {
      left--;
      if (next < chunks) {
        const t = new Uint8Array(4);
        t[0] = next & 0xff;
        t[1] = (next >> 8) & 0xff;
        t[2] = (next >> 16) & 0xff;
        t[3] = (next >> 24) & 0xff;
        wk.postMessage(t);
        next++;
      } else wk.terminate();
      if (left === 0) {
        process.stdout.write(`P4\n${w} ${h}\n`);
        process.stdout.write(image);
      }
    });
    wk.postMessage(image); // the shared image first, then one chunk at a time
    const t = new Uint8Array(4);
    t[0] = next & 0xff;
    t[1] = (next >> 8) & 0xff;
    t[2] = (next >> 16) & 0xff;
    t[3] = (next >> 24) & 0xff;
    wk.postMessage(t);
    next++;
  }
}

function workerThread(): void {
  const n: int = Math.trunc(Number(workerData));
  const bpr = (n + 7) >> 3;
  const shared: Uint8Array[] = []; // the image, once
  // each pixel's real part exactly as the original computes it, once per worker (the row's bytes are padded to 8)
  const cr = new Float64Array(bpr * 8);
  for (let x = 0; x < bpr * 8; x++) cr[x] = 2 * x / n - 1.5;
  parentPort.on("message", (m: Uint8Array) => {
    if (shared.length === 0) {
      shared.push(m);
      return;
    }
    const c: int = m[0] | (m[1] << 8) | (m[2] << 16) | (m[3] << 24);
    const y0 = c * ROWS;
    const rows = Math.min(ROWS, n - y0);
    const image = shared[0];
    for (let r = 0; r < rows; r++) row(y0 + r, n, n, image, (y0 + r) * bpr, cr);
    parentPort.postMessage(m);
  });
}

if (isMainThread) mainThread(Math.trunc(Number(process.argv[2])));
else workerThread();
