/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

mandelbrot, xisto #6: as #5 (chunks of 64 rows to a pool of workers writing into a shared image, the real parts
computed once per worker, the first iteration from zero as z = c, the escape tested every four iterations and only
after a byte that was all outside, all outside at a test: 0 at once), with sixteen pixels (two bytes) per call
instead of eight. Each pixel's z depends on its previous value through a multiply and two additions (9 cycles on
the Xeon's Broadwell), so with eight pixels, two vectors of four, the core waits on that chain; sixteen give it
twice the independent work for the same additions per pixel. Only z is kept from one iteration to the next (its
squares are computed at the start of each: fewer registers live), the escape test and the bits from |z|² of the
last z, and the row's last real part is read first, which tells the C compiler the fifteen before it are within
the array too (their checks go). The sixteen copies of each statement are one vector in xisto's C (lanes, see
src/emit.c), the same doubles as each pixel computed alone.
Xeon E5-2686 v4: 3,83 -> 2,45 s of CPU (Rust #4 3,57); Graviton2: 5,71 (Rust #4 5,47, #5 6,07; #5 with lanes 5,65).
 */

import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

const ROWS: int = 64; // rows per chunk

// the bits of sixteen pixels, the first in the highest bit: 1 = inside the set after 50 iterations
// (prev: the pixels before them in the row, 0 = all sixteen were outside, so these may escape too)
function bits16(r0: number, r1: number, r2: number, r3: number, r4: number, r5: number, r6: number, r7: number,
                r8: number, r9: number, r10: number, r11: number, r12: number, r13: number, r14: number, r15: number,
                ci: number, prev: int): int {
  // the first iteration from zero gives z = c exactly
  let zr0 = r0, zr1 = r1, zr2 = r2, zr3 = r3,
      zr4 = r4, zr5 = r5, zr6 = r6, zr7 = r7,
      zr8 = r8, zr9 = r9, zr10 = r10, zr11 = r11,
      zr12 = r12, zr13 = r13, zr14 = r14, zr15 = r15;
  let zi0 = ci, zi1 = ci, zi2 = ci, zi3 = ci,
      zi4 = ci, zi5 = ci, zi6 = ci, zi7 = ci,
      zi8 = ci, zi9 = ci, zi10 = ci, zi11 = ci,
      zi12 = ci, zi13 = ci, zi14 = ci, zi15 = ci;
  for (let i = 0; i < 12; i++) {
    for (let k = 0; k < 4; k++) {
      const tr0 = zr0 * zr0, tr1 = zr1 * zr1, tr2 = zr2 * zr2, tr3 = zr3 * zr3,
            tr4 = zr4 * zr4, tr5 = zr5 * zr5, tr6 = zr6 * zr6, tr7 = zr7 * zr7,
            tr8 = zr8 * zr8, tr9 = zr9 * zr9, tr10 = zr10 * zr10, tr11 = zr11 * zr11,
            tr12 = zr12 * zr12, tr13 = zr13 * zr13, tr14 = zr14 * zr14, tr15 = zr15 * zr15;
      const ti0 = zi0 * zi0, ti1 = zi1 * zi1, ti2 = zi2 * zi2, ti3 = zi3 * zi3,
            ti4 = zi4 * zi4, ti5 = zi5 * zi5, ti6 = zi6 * zi6, ti7 = zi7 * zi7,
            ti8 = zi8 * zi8, ti9 = zi9 * zi9, ti10 = zi10 * zi10, ti11 = zi11 * zi11,
            ti12 = zi12 * zi12, ti13 = zi13 * zi13, ti14 = zi14 * zi14, ti15 = zi15 * zi15;
      zi0 = 2 * zr0 * zi0 + ci; zi1 = 2 * zr1 * zi1 + ci; zi2 = 2 * zr2 * zi2 + ci; zi3 = 2 * zr3 * zi3 + ci;
      zi4 = 2 * zr4 * zi4 + ci; zi5 = 2 * zr5 * zi5 + ci; zi6 = 2 * zr6 * zi6 + ci; zi7 = 2 * zr7 * zi7 + ci;
      zi8 = 2 * zr8 * zi8 + ci; zi9 = 2 * zr9 * zi9 + ci; zi10 = 2 * zr10 * zi10 + ci; zi11 = 2 * zr11 * zi11 + ci;
      zi12 = 2 * zr12 * zi12 + ci; zi13 = 2 * zr13 * zi13 + ci; zi14 = 2 * zr14 * zi14 + ci; zi15 = 2 * zr15 * zi15 + ci;
      zr0 = tr0 - ti0 + r0; zr1 = tr1 - ti1 + r1; zr2 = tr2 - ti2 + r2; zr3 = tr3 - ti3 + r3;
      zr4 = tr4 - ti4 + r4; zr5 = tr5 - ti5 + r5; zr6 = tr6 - ti6 + r6; zr7 = tr7 - ti7 + r7;
      zr8 = tr8 - ti8 + r8; zr9 = tr9 - ti9 + r9; zr10 = tr10 - ti10 + r10; zr11 = tr11 - ti11 + r11;
      zr12 = tr12 - ti12 + r12; zr13 = tr13 - ti13 + r13; zr14 = tr14 - ti14 + r14; zr15 = tr15 - ti15 + r15;
    }
    if (prev === 0) {
      const m0 = zr0 * zr0 + zi0 * zi0, m1 = zr1 * zr1 + zi1 * zi1, m2 = zr2 * zr2 + zi2 * zi2, m3 = zr3 * zr3 + zi3 * zi3,
            m4 = zr4 * zr4 + zi4 * zi4, m5 = zr5 * zr5 + zi5 * zi5, m6 = zr6 * zr6 + zi6 * zi6, m7 = zr7 * zr7 + zi7 * zi7,
            m8 = zr8 * zr8 + zi8 * zi8, m9 = zr9 * zr9 + zi9 * zi9, m10 = zr10 * zr10 + zi10 * zi10, m11 = zr11 * zr11 + zi11 * zi11,
            m12 = zr12 * zr12 + zi12 * zi12, m13 = zr13 * zr13 + zi13 * zi13, m14 = zr14 * zr14 + zi14 * zi14, m15 = zr15 * zr15 + zi15 * zi15;
      const a = (!(m0 <= 4) && !(m1 <= 4) && !(m2 <= 4) && !(m3 <= 4)) && (!(m4 <= 4) && !(m5 <= 4) && !(m6 <= 4) && !(m7 <= 4));
      const b = (!(m8 <= 4) && !(m9 <= 4) && !(m10 <= 4) && !(m11 <= 4)) && (!(m12 <= 4) && !(m13 <= 4) && !(m14 <= 4) && !(m15 <= 4));
      if (a && b) return 0; // outside stays outside
    }
  }
  { // the 50th
    const tr0 = zr0 * zr0, tr1 = zr1 * zr1, tr2 = zr2 * zr2, tr3 = zr3 * zr3,
          tr4 = zr4 * zr4, tr5 = zr5 * zr5, tr6 = zr6 * zr6, tr7 = zr7 * zr7,
          tr8 = zr8 * zr8, tr9 = zr9 * zr9, tr10 = zr10 * zr10, tr11 = zr11 * zr11,
          tr12 = zr12 * zr12, tr13 = zr13 * zr13, tr14 = zr14 * zr14, tr15 = zr15 * zr15;
    const ti0 = zi0 * zi0, ti1 = zi1 * zi1, ti2 = zi2 * zi2, ti3 = zi3 * zi3,
          ti4 = zi4 * zi4, ti5 = zi5 * zi5, ti6 = zi6 * zi6, ti7 = zi7 * zi7,
          ti8 = zi8 * zi8, ti9 = zi9 * zi9, ti10 = zi10 * zi10, ti11 = zi11 * zi11,
          ti12 = zi12 * zi12, ti13 = zi13 * zi13, ti14 = zi14 * zi14, ti15 = zi15 * zi15;
    zi0 = 2 * zr0 * zi0 + ci; zi1 = 2 * zr1 * zi1 + ci; zi2 = 2 * zr2 * zi2 + ci; zi3 = 2 * zr3 * zi3 + ci;
    zi4 = 2 * zr4 * zi4 + ci; zi5 = 2 * zr5 * zi5 + ci; zi6 = 2 * zr6 * zi6 + ci; zi7 = 2 * zr7 * zi7 + ci;
    zi8 = 2 * zr8 * zi8 + ci; zi9 = 2 * zr9 * zi9 + ci; zi10 = 2 * zr10 * zi10 + ci; zi11 = 2 * zr11 * zi11 + ci;
    zi12 = 2 * zr12 * zi12 + ci; zi13 = 2 * zr13 * zi13 + ci; zi14 = 2 * zr14 * zi14 + ci; zi15 = 2 * zr15 * zi15 + ci;
    zr0 = tr0 - ti0 + r0; zr1 = tr1 - ti1 + r1; zr2 = tr2 - ti2 + r2; zr3 = tr3 - ti3 + r3;
    zr4 = tr4 - ti4 + r4; zr5 = tr5 - ti5 + r5; zr6 = tr6 - ti6 + r6; zr7 = tr7 - ti7 + r7;
    zr8 = tr8 - ti8 + r8; zr9 = tr9 - ti9 + r9; zr10 = tr10 - ti10 + r10; zr11 = tr11 - ti11 + r11;
    zr12 = tr12 - ti12 + r12; zr13 = tr13 - ti13 + r13; zr14 = tr14 - ti14 + r14; zr15 = tr15 - ti15 + r15;
  }
  let b = 0;
  if (zr0 * zr0 + zi0 * zi0 <= 4) b |= 32768;
  if (zr1 * zr1 + zi1 * zi1 <= 4) b |= 16384;
  if (zr2 * zr2 + zi2 * zi2 <= 4) b |= 8192;
  if (zr3 * zr3 + zi3 * zi3 <= 4) b |= 4096;
  if (zr4 * zr4 + zi4 * zi4 <= 4) b |= 2048;
  if (zr5 * zr5 + zi5 * zi5 <= 4) b |= 1024;
  if (zr6 * zr6 + zi6 * zi6 <= 4) b |= 512;
  if (zr7 * zr7 + zi7 * zi7 <= 4) b |= 256;
  if (zr8 * zr8 + zi8 * zi8 <= 4) b |= 128;
  if (zr9 * zr9 + zi9 * zi9 <= 4) b |= 64;
  if (zr10 * zr10 + zi10 * zi10 <= 4) b |= 32;
  if (zr11 * zr11 + zi11 * zi11 <= 4) b |= 16;
  if (zr12 * zr12 + zi12 * zi12 <= 4) b |= 8;
  if (zr13 * zr13 + zi13 * zi13 <= 4) b |= 4;
  if (zr14 * zr14 + zi14 * zi14 <= 4) b |= 2;
  if (zr15 * zr15 + zi15 * zi15 <= 4) b |= 1;
  return b;
}

// the bits of one row, eight pixels per byte, into out from at (cr: the real parts, padded to whole sixteens)
function row(y: int, w: int, h: int, out: Uint8Array, at: int, cr: Float64Array): void {
  const ci = 2 * y / h - 1;
  let prev = 0;
  for (let x = 0; x < w; x += 16) {
    const last = cr[x + 15]; // the last first: the fifteen before it are then known to be within cr
    let b = bits16(cr[x], cr[x + 1], cr[x + 2], cr[x + 3], cr[x + 4], cr[x + 5], cr[x + 6], cr[x + 7],
                   cr[x + 8], cr[x + 9], cr[x + 10], cr[x + 11], cr[x + 12], cr[x + 13], cr[x + 14], last, ci, prev);
    const past = x + 16 - w; // pixels beyond the row's end: zero bits, as the original pads
    if (past > 0) b &= (0xffff << past) & 0xffff;
    out[at + (x >> 3)] = b >> 8;
    if (x + 8 < w) out[at + (x >> 3) + 1] = b & 0xff;
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
  // each pixel's real part exactly as the original computes it, once per worker (padded to whole sixteens)
  const cr = new Float64Array(bpr * 8 + 16);
  for (let x = 0; x < bpr * 8 + 16; x++) cr[x] = 2 * x / n - 1.5;
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
