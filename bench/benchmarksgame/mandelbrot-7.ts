/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

mandelbrot, xisto #7: as #5 (chunks of 64 rows to a pool of workers writing into a shared image, the real parts
computed once per worker, eight pixels per call, the first iteration from zero as z = c, all eight outside at a
test: 0 at once), with the two kinds of byte apart. After a byte with a pixel inside the set nothing is tested: the
49 iterations run in a loop of their own, with no test of prev in it. After a byte all outside, the test comes
early and often (every 3 iterations up to the 12th, then every 4 up to the 28th, then every 8), when most of the
pixels that escape at all do. Only z is kept between iterations (its squares computed at the start of each), and
the row's last real part is read first (the seven before it then need no check). The row goes by its bytes (k++,
the last one masked inside the loop: the stores need no check and no overflow test per byte, 5,45 -> 5,41 s of
CPU), and the rows go out in pieces of 64 that shrink near the end (a quarter of what is left per worker, at least
4), so that the two workers finish together.
Graviton2 (16000, 2 threads, 9 runs): 2,783 s / 5,46 u (#5 2,891 / 5,67; Rust #4 2,801 / 5,49).
 */

import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

const ROWS: int = 64; // rows per chunk

// the byte of eight pixels, the first in the highest bit: 1 = inside the set after 50 iterations
// (prev: the byte before it in the row, 0 = all eight were outside, so these may escape too)
function byte8(r0: number, r1: number, r2: number, r3: number, r4: number, r5: number, r6: number, r7: number,
               ci: number, prev: int): int {
  // the first iteration from zero gives z = c exactly
  let zr0 = r0, zr1 = r1, zr2 = r2, zr3 = r3,
      zr4 = r4, zr5 = r5, zr6 = r6, zr7 = r7;
  let zi0 = ci, zi1 = ci, zi2 = ci, zi3 = ci,
      zi4 = ci, zi5 = ci, zi6 = ci, zi7 = ci;
  if (prev === 0) { // tested: often early (most pixels that escape do it within a few iterations), rarely late
    for (let i = 0; i < 4; i++) {
      for (let k = 0; k < 3; k++) {
        const tr0 = zr0 * zr0, tr1 = zr1 * zr1, tr2 = zr2 * zr2, tr3 = zr3 * zr3,
              tr4 = zr4 * zr4, tr5 = zr5 * zr5, tr6 = zr6 * zr6, tr7 = zr7 * zr7;
        const ti0 = zi0 * zi0, ti1 = zi1 * zi1, ti2 = zi2 * zi2, ti3 = zi3 * zi3,
              ti4 = zi4 * zi4, ti5 = zi5 * zi5, ti6 = zi6 * zi6, ti7 = zi7 * zi7;
        zi0 = 2 * zr0 * zi0 + ci; zi1 = 2 * zr1 * zi1 + ci; zi2 = 2 * zr2 * zi2 + ci; zi3 = 2 * zr3 * zi3 + ci;
        zi4 = 2 * zr4 * zi4 + ci; zi5 = 2 * zr5 * zi5 + ci; zi6 = 2 * zr6 * zi6 + ci; zi7 = 2 * zr7 * zi7 + ci;
        zr0 = tr0 - ti0 + r0; zr1 = tr1 - ti1 + r1; zr2 = tr2 - ti2 + r2; zr3 = tr3 - ti3 + r3;
        zr4 = tr4 - ti4 + r4; zr5 = tr5 - ti5 + r5; zr6 = tr6 - ti6 + r6; zr7 = tr7 - ti7 + r7;
      }
      const m0 = zr0 * zr0 + zi0 * zi0, m1 = zr1 * zr1 + zi1 * zi1, m2 = zr2 * zr2 + zi2 * zi2, m3 = zr3 * zr3 + zi3 * zi3,
            m4 = zr4 * zr4 + zi4 * zi4, m5 = zr5 * zr5 + zi5 * zi5, m6 = zr6 * zr6 + zi6 * zi6, m7 = zr7 * zr7 + zi7 * zi7;
      if (!(m0 <= 4) && !(m1 <= 4) && !(m2 <= 4) && !(m3 <= 4) &&
          !(m4 <= 4) && !(m5 <= 4) && !(m6 <= 4) && !(m7 <= 4)) return 0; // outside stays outside
    }
    for (let i = 0; i < 4; i++) {
      for (let k = 0; k < 4; k++) {
        const tr0 = zr0 * zr0, tr1 = zr1 * zr1, tr2 = zr2 * zr2, tr3 = zr3 * zr3,
              tr4 = zr4 * zr4, tr5 = zr5 * zr5, tr6 = zr6 * zr6, tr7 = zr7 * zr7;
        const ti0 = zi0 * zi0, ti1 = zi1 * zi1, ti2 = zi2 * zi2, ti3 = zi3 * zi3,
              ti4 = zi4 * zi4, ti5 = zi5 * zi5, ti6 = zi6 * zi6, ti7 = zi7 * zi7;
        zi0 = 2 * zr0 * zi0 + ci; zi1 = 2 * zr1 * zi1 + ci; zi2 = 2 * zr2 * zi2 + ci; zi3 = 2 * zr3 * zi3 + ci;
        zi4 = 2 * zr4 * zi4 + ci; zi5 = 2 * zr5 * zi5 + ci; zi6 = 2 * zr6 * zi6 + ci; zi7 = 2 * zr7 * zi7 + ci;
        zr0 = tr0 - ti0 + r0; zr1 = tr1 - ti1 + r1; zr2 = tr2 - ti2 + r2; zr3 = tr3 - ti3 + r3;
        zr4 = tr4 - ti4 + r4; zr5 = tr5 - ti5 + r5; zr6 = tr6 - ti6 + r6; zr7 = tr7 - ti7 + r7;
      }
      const m0 = zr0 * zr0 + zi0 * zi0, m1 = zr1 * zr1 + zi1 * zi1, m2 = zr2 * zr2 + zi2 * zi2, m3 = zr3 * zr3 + zi3 * zi3,
            m4 = zr4 * zr4 + zi4 * zi4, m5 = zr5 * zr5 + zi5 * zi5, m6 = zr6 * zr6 + zi6 * zi6, m7 = zr7 * zr7 + zi7 * zi7;
      if (!(m0 <= 4) && !(m1 <= 4) && !(m2 <= 4) && !(m3 <= 4) &&
          !(m4 <= 4) && !(m5 <= 4) && !(m6 <= 4) && !(m7 <= 4)) return 0; // outside stays outside
    }
    for (let i = 0; i < 2; i++) {
      for (let k = 0; k < 8; k++) {
        const tr0 = zr0 * zr0, tr1 = zr1 * zr1, tr2 = zr2 * zr2, tr3 = zr3 * zr3,
              tr4 = zr4 * zr4, tr5 = zr5 * zr5, tr6 = zr6 * zr6, tr7 = zr7 * zr7;
        const ti0 = zi0 * zi0, ti1 = zi1 * zi1, ti2 = zi2 * zi2, ti3 = zi3 * zi3,
              ti4 = zi4 * zi4, ti5 = zi5 * zi5, ti6 = zi6 * zi6, ti7 = zi7 * zi7;
        zi0 = 2 * zr0 * zi0 + ci; zi1 = 2 * zr1 * zi1 + ci; zi2 = 2 * zr2 * zi2 + ci; zi3 = 2 * zr3 * zi3 + ci;
        zi4 = 2 * zr4 * zi4 + ci; zi5 = 2 * zr5 * zi5 + ci; zi6 = 2 * zr6 * zi6 + ci; zi7 = 2 * zr7 * zi7 + ci;
        zr0 = tr0 - ti0 + r0; zr1 = tr1 - ti1 + r1; zr2 = tr2 - ti2 + r2; zr3 = tr3 - ti3 + r3;
        zr4 = tr4 - ti4 + r4; zr5 = tr5 - ti5 + r5; zr6 = tr6 - ti6 + r6; zr7 = tr7 - ti7 + r7;
      }
      const m0 = zr0 * zr0 + zi0 * zi0, m1 = zr1 * zr1 + zi1 * zi1, m2 = zr2 * zr2 + zi2 * zi2, m3 = zr3 * zr3 + zi3 * zi3,
            m4 = zr4 * zr4 + zi4 * zi4, m5 = zr5 * zr5 + zi5 * zi5, m6 = zr6 * zr6 + zi6 * zi6, m7 = zr7 * zr7 + zi7 * zi7;
      if (!(m0 <= 4) && !(m1 <= 4) && !(m2 <= 4) && !(m3 <= 4) &&
          !(m4 <= 4) && !(m5 <= 4) && !(m6 <= 4) && !(m7 <= 4)) return 0; // outside stays outside
    }
    for (let k = 0; k < 5; k++) {
      const tr0 = zr0 * zr0, tr1 = zr1 * zr1, tr2 = zr2 * zr2, tr3 = zr3 * zr3,
            tr4 = zr4 * zr4, tr5 = zr5 * zr5, tr6 = zr6 * zr6, tr7 = zr7 * zr7;
      const ti0 = zi0 * zi0, ti1 = zi1 * zi1, ti2 = zi2 * zi2, ti3 = zi3 * zi3,
            ti4 = zi4 * zi4, ti5 = zi5 * zi5, ti6 = zi6 * zi6, ti7 = zi7 * zi7;
      zi0 = 2 * zr0 * zi0 + ci; zi1 = 2 * zr1 * zi1 + ci; zi2 = 2 * zr2 * zi2 + ci; zi3 = 2 * zr3 * zi3 + ci;
      zi4 = 2 * zr4 * zi4 + ci; zi5 = 2 * zr5 * zi5 + ci; zi6 = 2 * zr6 * zi6 + ci; zi7 = 2 * zr7 * zi7 + ci;
      zr0 = tr0 - ti0 + r0; zr1 = tr1 - ti1 + r1; zr2 = tr2 - ti2 + r2; zr3 = tr3 - ti3 + r3;
      zr4 = tr4 - ti4 + r4; zr5 = tr5 - ti5 + r5; zr6 = tr6 - ti6 + r6; zr7 = tr7 - ti7 + r7;
    }
  } else { // inside the set nothing escapes: the 49 iterations with no test
    for (let k = 0; k < 49; k++) {
      const tr0 = zr0 * zr0, tr1 = zr1 * zr1, tr2 = zr2 * zr2, tr3 = zr3 * zr3,
            tr4 = zr4 * zr4, tr5 = zr5 * zr5, tr6 = zr6 * zr6, tr7 = zr7 * zr7;
      const ti0 = zi0 * zi0, ti1 = zi1 * zi1, ti2 = zi2 * zi2, ti3 = zi3 * zi3,
            ti4 = zi4 * zi4, ti5 = zi5 * zi5, ti6 = zi6 * zi6, ti7 = zi7 * zi7;
      zi0 = 2 * zr0 * zi0 + ci; zi1 = 2 * zr1 * zi1 + ci; zi2 = 2 * zr2 * zi2 + ci; zi3 = 2 * zr3 * zi3 + ci;
      zi4 = 2 * zr4 * zi4 + ci; zi5 = 2 * zr5 * zi5 + ci; zi6 = 2 * zr6 * zi6 + ci; zi7 = 2 * zr7 * zi7 + ci;
      zr0 = tr0 - ti0 + r0; zr1 = tr1 - ti1 + r1; zr2 = tr2 - ti2 + r2; zr3 = tr3 - ti3 + r3;
      zr4 = tr4 - ti4 + r4; zr5 = tr5 - ti5 + r5; zr6 = tr6 - ti6 + r6; zr7 = tr7 - ti7 + r7;
    }
  }
  let b = 0;
  if (zr0 * zr0 + zi0 * zi0 <= 4) b |= 128;
  if (zr1 * zr1 + zi1 * zi1 <= 4) b |= 64;
  if (zr2 * zr2 + zi2 * zi2 <= 4) b |= 32;
  if (zr3 * zr3 + zi3 * zi3 <= 4) b |= 16;
  if (zr4 * zr4 + zi4 * zi4 <= 4) b |= 8;
  if (zr5 * zr5 + zi5 * zi5 <= 4) b |= 4;
  if (zr6 * zr6 + zi6 * zi6 <= 4) b |= 2;
  if (zr7 * zr7 + zi7 * zi7 <= 4) b |= 1;
  return b;
}

// the bits of one row, eight pixels per byte, into out from at (cr: the real parts, padded to whole bytes)
function row(y: int, w: int, h: int, out: Uint8Array, at: int, cr: Float64Array): void {
  const ci = 2 * y / h - 1;
  const bytes = (w + 7) >> 3, past = bytes * 8 - w; // past: the last byte's pixels beyond the row's end
  let prev = 0;
  for (let k = 0; k < bytes; k++) {
    const x = k << 3;
    const last = cr[x + 7]; // the last first: the seven before it are then known to be within cr
    let b = byte8(cr[x], cr[x + 1], cr[x + 2], cr[x + 3], cr[x + 4], cr[x + 5], cr[x + 6], last, ci, prev);
    if (k === bytes - 1) b &= (0xff << past) & 0xff; // zero bits there, as the original pads
    out[at + k] = b;
    prev = b;
  }
}

// an order for a worker: its first row and how many, four bytes each
function order(y0: int, rows: int): Uint8Array {
  const t = new Uint8Array(8);
  for (let k = 0; k < 4; k++) {
    t[k] = (y0 >> (8 * k)) & 0xff;
    t[4 + k] = (rows >> (8 * k)) & 0xff;
  }
  return t;
}

function mainThread(n: int): void {
  const w = n, h = n;
  const bpr = (w + 7) >> 3;
  const image = new Uint8Array(new SharedArrayBuffer(bpr * h));
  const nworkers = Math.max(1, Math.min(availableParallelism(), h));
  let next: int = 0, out: int = 0; // the next row to hand out; the orders not back yet
  // pieces of at most ROWS rows that shrink near the end (a quarter of what is left per worker, at least 4), so the
  // workers finish together
  const give = (wk: Worker): boolean => {
    if (next >= h) return false;
    const rows: int = Math.min(h - next, Math.max(4, Math.min(ROWS, Math.trunc((h - next) / (4 * nworkers)))));
    wk.postMessage(order(next, rows));
    next += rows;
    out++;
    return true;
  };
  for (let k = 0; k < nworkers; k++) {
    const wk = new Worker(__filename, { workerData: `${n}` });
    wk.on("message", (m: Uint8Array) => {
      out--;
      if (!give(wk)) wk.terminate();
      if (out === 0 && next >= h) {
        process.stdout.write(`P4\n${w} ${h}\n`);
        process.stdout.write(image);
      }
    });
    wk.postMessage(image); // the shared image first, then one order at a time
    if (!give(wk)) wk.terminate(); // (more workers than pieces)
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
    const y0: int = m[0] | (m[1] << 8) | (m[2] << 16) | (m[3] << 24);
    const rows: int = m[4] | (m[5] << 8) | (m[6] << 16) | (m[7] << 24);
    const image = shared[0];
    for (let r = 0; r < rows; r++) row(y0 + r, n, n, image, (y0 + r) * bpr, cr);
    parentPort.postMessage(m);
  });
}

if (isMainThread) mainThread(Math.trunc(Number(process.argv[2])));
else workerThread();
