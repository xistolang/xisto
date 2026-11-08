/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

mandelbrot, xisto #3: as #2 (chunks of rows to a pool of workers, eight pixels at a time as eight independent
sets of variables, the escape tested every five iterations), with the test shaped like the arithmetic: the
eight sums first, then the comparisons reduced in pairs, (0,1) (2,3) (4,5) (6,7), the same pairing the C
compiler uses for the vector arithmetic, so no lane has to move between vectors for the test. And a byte is
only tested for escape when the previous byte of the row was all outside the set (0): inside the set nothing
escapes, so the test would only cost (the 50 iterations run either way, the bits come out the same).
 */

import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

const ROWS: int = 16; // rows per chunk

// the bits of one row, eight pixels per byte, into out from at
function row(y: int, w: int, h: int, out: Uint8Array, at: int): void {
  const ci = 2 * y / h - 1;
  let prev = 0; // the previous byte of the row: 0 = all eight outside
  for (let x = 0; x < w; x += 8) {
    // each pixel's real part exactly as the original computes it (c0 + k * step would round differently)
    const r0 = 2 * x / w - 1.5, r1 = 2 * (x + 1) / w - 1.5, r2 = 2 * (x + 2) / w - 1.5, r3 = 2 * (x + 3) / w - 1.5;
    const r4 = 2 * (x + 4) / w - 1.5, r5 = 2 * (x + 5) / w - 1.5, r6 = 2 * (x + 6) / w - 1.5, r7 = 2 * (x + 7) / w - 1.5;
    let zr0 = 0, zr1 = 0, zr2 = 0, zr3 = 0, zr4 = 0, zr5 = 0, zr6 = 0, zr7 = 0;
    let zi0 = 0, zi1 = 0, zi2 = 0, zi3 = 0, zi4 = 0, zi5 = 0, zi6 = 0, zi7 = 0;
    let tr0 = 0, tr1 = 0, tr2 = 0, tr3 = 0, tr4 = 0, tr5 = 0, tr6 = 0, tr7 = 0;
    let ti0 = 0, ti1 = 0, ti2 = 0, ti3 = 0, ti4 = 0, ti5 = 0, ti6 = 0, ti7 = 0;
    for (let i = 0; i < 10; i++) {
      for (let k = 0; k < 5; k++) {
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
        if (lo && hi) break;
      }
    }
    let b = 0;
    if (tr0 + ti0 <= 4) b |= 128;
    if (tr1 + ti1 <= 4) b |= 64;
    if (tr2 + ti2 <= 4) b |= 32;
    if (tr3 + ti3 <= 4) b |= 16;
    if (tr4 + ti4 <= 4) b |= 8;
    if (tr5 + ti5 <= 4) b |= 4;
    if (tr6 + ti6 <= 4) b |= 2;
    if (tr7 + ti7 <= 4) b |= 1;
    const past = x + 8 - w; // pixels beyond the row's end (its last byte): zero bits, as the original pads
    if (past > 0) b &= (0xff << past) & 0xff;
    out[at + (x >> 3)] = b;
    prev = b;
  }
}

function mainThread(n: int): void {
  const w = n, h = n;
  const bpr = (w + 7) >> 3;
  const image = new Uint8Array(bpr * h);
  const chunks = Math.trunc((h + ROWS - 1) / ROWS);
  let next = 0, left = chunks;
  const nworkers = Math.max(1, Math.min(availableParallelism(), chunks));
  for (let k = 0; k < nworkers; k++) {
    const wk = new Worker(__filename, { workerData: `${n}` });
    wk.on("message", (m: Uint8Array) => {
      const c = m[0] | (m[1] << 8) | (m[2] << 16) | (m[3] << 24); // the chunk, in the first four bytes
      image.set(m.subarray(4), c * ROWS * bpr);
      left--;
      if (next < chunks) {
        wk.postMessage(`${next}`);
        next++;
      } else wk.terminate();
      if (left === 0) {
        process.stdout.write(`P4\n${w} ${h}\n`);
        process.stdout.write(image);
      }
    });
    wk.postMessage(`${next}`);
    next++;
  }
}

function workerThread(): void {
  const n: int = Math.trunc(Number(workerData));
  const bpr = (n + 7) >> 3;
  parentPort.on("message", (m: string) => {
    const c: int = Math.trunc(Number(m));
    const y0 = c * ROWS;
    const rows = Math.min(ROWS, n - y0);
    const out = new Uint8Array(4 + rows * bpr);
    out[0] = c & 0xff;
    out[1] = (c >> 8) & 0xff;
    out[2] = (c >> 16) & 0xff;
    out[3] = (c >> 24) & 0xff;
    for (let r = 0; r < rows; r++) row(y0 + r, n, n, out, 4 + r * bpr);
    parentPort.postMessage(out);
  });
}

if (isMainThread) mainThread(Math.trunc(Number(process.argv[2])));
else workerThread();
