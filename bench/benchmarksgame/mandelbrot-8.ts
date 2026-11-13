/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

mandelbrot, xisto #8: as #7, with the three bytes after a byte with a pixel inside the set computed at once, as two
runs of 12 pixels through the 49 iterations with no test (inside12), and 2·zr·zi written p + p with p = zr·zi (the
same double: doubling is exact) everywhere: an addition (2 cycles) where the multiplication by 2 took 3, on the
chain each iteration waits for. On Neoverse-N1 the 49 iterations of 8 pixels at a time (#7) go at 2,06-2,10 cycles
per pixel and iteration; 12 at a time, 1,93; 16 at a time no longer fit in the registers. The compiler takes the
12 copies as three vectors of 4 (lane copies: the N that puts the most statements in vectors).
Graviton2 (16000, 2 threads, 9 runs interleaved): 2,624 s / 5,20 s of CPU (#7 2,756 / 5,45; Rust #4 2,798 / 5,50).
On the Xeon (AVX2, 4 doubles a vector) it is slower than #6 (1,59 s against 1,33): there #6 stays.
The rest as #7: chunks of 64 rows to a pool of workers writing into a shared image, the real parts computed once
per worker, the first iteration from zero as z = c, all eight outside at a test: 0 at once; after a byte all
outside the test comes early and often (every 3 iterations up to the 12th, then every 4 up to the 28th, then every
8); the image goes out in order through a ring of 2560 rows, in writes of 512 rows.
 */

import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

const ROWS: int = 64; // rows per chunk
const RING: int = 2560; // rows held at once (5 MB at 16000 pixels a row)
const BATCH: int = 512; // rows written at a time (1 MB at 16000: few big writes)

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
        const p0 = zr0 * zi0, p1 = zr1 * zi1, p2 = zr2 * zi2, p3 = zr3 * zi3,
              p4 = zr4 * zi4, p5 = zr5 * zi5, p6 = zr6 * zi6, p7 = zr7 * zi7;
        zi0 = p0 + p0 + ci; zi1 = p1 + p1 + ci; zi2 = p2 + p2 + ci; zi3 = p3 + p3 + ci;
        zi4 = p4 + p4 + ci; zi5 = p5 + p5 + ci; zi6 = p6 + p6 + ci; zi7 = p7 + p7 + ci;
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
        const p0 = zr0 * zi0, p1 = zr1 * zi1, p2 = zr2 * zi2, p3 = zr3 * zi3,
              p4 = zr4 * zi4, p5 = zr5 * zi5, p6 = zr6 * zi6, p7 = zr7 * zi7;
        zi0 = p0 + p0 + ci; zi1 = p1 + p1 + ci; zi2 = p2 + p2 + ci; zi3 = p3 + p3 + ci;
        zi4 = p4 + p4 + ci; zi5 = p5 + p5 + ci; zi6 = p6 + p6 + ci; zi7 = p7 + p7 + ci;
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
        const p0 = zr0 * zi0, p1 = zr1 * zi1, p2 = zr2 * zi2, p3 = zr3 * zi3,
              p4 = zr4 * zi4, p5 = zr5 * zi5, p6 = zr6 * zi6, p7 = zr7 * zi7;
        zi0 = p0 + p0 + ci; zi1 = p1 + p1 + ci; zi2 = p2 + p2 + ci; zi3 = p3 + p3 + ci;
        zi4 = p4 + p4 + ci; zi5 = p5 + p5 + ci; zi6 = p6 + p6 + ci; zi7 = p7 + p7 + ci;
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
      const p0 = zr0 * zi0, p1 = zr1 * zi1, p2 = zr2 * zi2, p3 = zr3 * zi3,
            p4 = zr4 * zi4, p5 = zr5 * zi5, p6 = zr6 * zi6, p7 = zr7 * zi7;
      zi0 = p0 + p0 + ci; zi1 = p1 + p1 + ci; zi2 = p2 + p2 + ci; zi3 = p3 + p3 + ci;
      zi4 = p4 + p4 + ci; zi5 = p5 + p5 + ci; zi6 = p6 + p6 + ci; zi7 = p7 + p7 + ci;
      zr0 = tr0 - ti0 + r0; zr1 = tr1 - ti1 + r1; zr2 = tr2 - ti2 + r2; zr3 = tr3 - ti3 + r3;
      zr4 = tr4 - ti4 + r4; zr5 = tr5 - ti5 + r5; zr6 = tr6 - ti6 + r6; zr7 = tr7 - ti7 + r7;
    }
  } else { // inside the set nothing escapes: the 49 iterations with no test
    for (let k = 0; k < 49; k++) {
      const tr0 = zr0 * zr0, tr1 = zr1 * zr1, tr2 = zr2 * zr2, tr3 = zr3 * zr3,
            tr4 = zr4 * zr4, tr5 = zr5 * zr5, tr6 = zr6 * zr6, tr7 = zr7 * zr7;
      const ti0 = zi0 * zi0, ti1 = zi1 * zi1, ti2 = zi2 * zi2, ti3 = zi3 * zi3,
            ti4 = zi4 * zi4, ti5 = zi5 * zi5, ti6 = zi6 * zi6, ti7 = zi7 * zi7;
      const p0 = zr0 * zi0, p1 = zr1 * zi1, p2 = zr2 * zi2, p3 = zr3 * zi3,
            p4 = zr4 * zi4, p5 = zr5 * zi5, p6 = zr6 * zi6, p7 = zr7 * zi7;
      zi0 = p0 + p0 + ci; zi1 = p1 + p1 + ci; zi2 = p2 + p2 + ci; zi3 = p3 + p3 + ci;
      zi4 = p4 + p4 + ci; zi5 = p5 + p5 + ci; zi6 = p6 + p6 + ci; zi7 = p7 + p7 + ci;
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

// 12 pixels (a byte and a half: two of these take the three bytes after a byte with a pixel inside), the 49
// iterations with no test, as byte8's, the first pixel in the highest of the 12 bits. Twelve chains of iterations
// at once (six vector registers of two doubles on arm64) keep the multipliers and adders busier than eight do,
// and z, its imaginary part and c still fit in the registers (sixteen pixels: 24 of the 32, and spills)
function inside12(r0: number, r1: number, r2: number, r3: number, r4: number, r5: number, r6: number, r7: number,
                  r8: number, r9: number, r10: number, r11: number, ci: number): int {
  let zr0 = r0, zr1 = r1, zr2 = r2, zr3 = r3, zr4 = r4, zr5 = r5, zr6 = r6, zr7 = r7, zr8 = r8, zr9 = r9, zr10 = r10,
      zr11 = r11;
  let zi0 = ci, zi1 = ci, zi2 = ci, zi3 = ci, zi4 = ci, zi5 = ci, zi6 = ci, zi7 = ci, zi8 = ci, zi9 = ci, zi10 = ci,
      zi11 = ci;
  for (let k = 0; k < 49; k++) {
    const tr0 = zr0 * zr0, tr1 = zr1 * zr1, tr2 = zr2 * zr2, tr3 = zr3 * zr3, tr4 = zr4 * zr4, tr5 = zr5 * zr5,
          tr6 = zr6 * zr6, tr7 = zr7 * zr7, tr8 = zr8 * zr8, tr9 = zr9 * zr9, tr10 = zr10 * zr10, tr11 = zr11 * zr11;
    const ti0 = zi0 * zi0, ti1 = zi1 * zi1, ti2 = zi2 * zi2, ti3 = zi3 * zi3, ti4 = zi4 * zi4, ti5 = zi5 * zi5,
          ti6 = zi6 * zi6, ti7 = zi7 * zi7, ti8 = zi8 * zi8, ti9 = zi9 * zi9, ti10 = zi10 * zi10, ti11 = zi11 * zi11;
    const p0 = zr0 * zi0, p1 = zr1 * zi1, p2 = zr2 * zi2, p3 = zr3 * zi3, p4 = zr4 * zi4, p5 = zr5 * zi5,
          p6 = zr6 * zi6, p7 = zr7 * zi7, p8 = zr8 * zi8, p9 = zr9 * zi9, p10 = zr10 * zi10, p11 = zr11 * zi11;
    zi0 = p0 + p0 + ci; zi1 = p1 + p1 + ci; zi2 = p2 + p2 + ci; zi3 = p3 + p3 + ci; zi4 = p4 + p4 + ci;
    zi5 = p5 + p5 + ci; zi6 = p6 + p6 + ci; zi7 = p7 + p7 + ci; zi8 = p8 + p8 + ci; zi9 = p9 + p9 + ci;
    zi10 = p10 + p10 + ci; zi11 = p11 + p11 + ci;
    zr0 = tr0 - ti0 + r0; zr1 = tr1 - ti1 + r1; zr2 = tr2 - ti2 + r2; zr3 = tr3 - ti3 + r3; zr4 = tr4 - ti4 + r4;
    zr5 = tr5 - ti5 + r5; zr6 = tr6 - ti6 + r6; zr7 = tr7 - ti7 + r7; zr8 = tr8 - ti8 + r8; zr9 = tr9 - ti9 + r9;
    zr10 = tr10 - ti10 + r10; zr11 = tr11 - ti11 + r11;
  }
  let b = 0;
  if (zr0 * zr0 + zi0 * zi0 <= 4) b |= 2048;
  if (zr1 * zr1 + zi1 * zi1 <= 4) b |= 1024;
  if (zr2 * zr2 + zi2 * zi2 <= 4) b |= 512;
  if (zr3 * zr3 + zi3 * zi3 <= 4) b |= 256;
  if (zr4 * zr4 + zi4 * zi4 <= 4) b |= 128;
  if (zr5 * zr5 + zi5 * zi5 <= 4) b |= 64;
  if (zr6 * zr6 + zi6 * zi6 <= 4) b |= 32;
  if (zr7 * zr7 + zi7 * zi7 <= 4) b |= 16;
  if (zr8 * zr8 + zi8 * zi8 <= 4) b |= 8;
  if (zr9 * zr9 + zi9 * zi9 <= 4) b |= 4;
  if (zr10 * zr10 + zi10 * zi10 <= 4) b |= 2;
  if (zr11 * zr11 + zi11 * zi11 <= 4) b |= 1;
  return b;
}

// the bits of one row, eight pixels per byte, into out from at (cr: the real parts, padded to whole bytes)
function row(y: int, w: int, h: int, out: Uint8Array, at: int, cr: Float64Array): void {
  const ci = 2 * y / h - 1;
  const bytes = (w + 7) >> 3, past = bytes * 8 - w; // past: the last byte's pixels beyond the row's end
  let prev = 0;
  for (let k = 0; k < bytes; k++) {
    const x = k << 3;
    if (prev !== 0 && k + 3 < bytes) { // the next three bytes likely inside too (none of them the row's last)
      const last = cr[x + 23]; // read first: the ones before it are then within cr
      const v = (inside12(cr[x + 0], cr[x + 1], cr[x + 2], cr[x + 3], cr[x + 4], cr[x + 5],
                          cr[x + 6], cr[x + 7], cr[x + 8], cr[x + 9], cr[x + 10], cr[x + 11], ci) << 12) |
                inside12(cr[x + 12], cr[x + 13], cr[x + 14], cr[x + 15], cr[x + 16], cr[x + 17],
                         cr[x + 18], cr[x + 19], cr[x + 20], cr[x + 21], cr[x + 22], last, ci);
      out[at + k] = (v >> 16) & 0xff;
      out[at + k + 1] = (v >> 8) & 0xff;
      out[at + k + 2] = v & 0xff;
      prev = v & 0xff;
      k += 2;
      continue;
    }
    const last = cr[x + 7]; // the last first: the seven before it are then known to be within cr
    let b = byte8(cr[x], cr[x + 1], cr[x + 2], cr[x + 3], cr[x + 4], cr[x + 5], cr[x + 6], last, ci, prev);
    if (k === bytes - 1) b &= (0xff << past) & 0xff; // zero bits there, as the original pads
    out[at + k] = b;
    prev = b;
  }
}

// an order for a worker: its first row and how many, four bytes each
function order(y0: int, rows: int, piece: int): Uint8Array {
  const t = new Uint8Array(12);
  for (let k = 0; k < 4; k++) {
    t[k] = (y0 >> (8 * k)) & 0xff;
    t[4 + k] = (rows >> (8 * k)) & 0xff;
    t[8 + k] = (piece >> (8 * k)) & 0xff;
  }
  return t;
}

function mainThread(n: int): void {
  const w = n, h = n;
  const bpr = (w + 7) >> 3;
  // the rows go through a ring of RING rows (row y at y % RING): a piece is handed out only when its rows are free
  // (the rows RING before them already written), and the rows done in order are written BATCH at a time
  const ring = Math.min(RING, h);
  const image = new Uint8Array(new SharedArrayBuffer(bpr * ring));
  const nworkers = Math.max(1, Math.min(availableParallelism(), h));
  process.stdout.write(`P4\n${w} ${h}\n`);
  let next: int = 0, written: int = 0; // the next row to hand out; the rows written so far
  const starts: int[] = [], ends: int[] = []; // the pieces handed out, in order
  const done: boolean[] = [];
  let first: int = 0; // the first piece not yet known done
  let ready: int = 0; // the rows done in order (written, or to be in the next batch)
  const idle: Worker[] = [];
  // a piece for wk, when there is one and its rows are free
  const give = (wk: Worker): boolean => {
    if (next >= h) return false;
    const rows: int = Math.min(h - next, Math.max(4, Math.min(ROWS, Math.trunc((h - next) / (4 * nworkers)))));
    if (next + rows - written > ring) { idle.push(wk); return true; }
    wk.postMessage(order(next, rows, done.length));
    starts.push(next);
    ends.push(next + rows);
    done.push(false);
    next += rows;
    return true;
  };
  const workers: Worker[] = [];
  for (let k = 0; k < nworkers; k++) {
    const wk = new Worker(__filename, { workerData: `${n}` });
    workers.push(wk);
    wk.on("message", (m: Uint8Array) => {
      done[m[8] | (m[9] << 8) | (m[10] << 16) | (m[11] << 24)] = true;
      while (first < done.length && done[first]) { ready = ends[first]; first++; }
      if (ready - written >= BATCH || ready >= h) { // out in order, a batch at a time: one or two runs of the ring
        const a: int = written % ring, b: int = a + ready - written;
        if (b <= ring) process.stdout.write(image.subarray(a * bpr, b * bpr));
        else {
          process.stdout.write(image.subarray(a * bpr, ring * bpr));
          process.stdout.write(image.subarray(0, (b - ring) * bpr));
        }
        written = ready;
      }
      if (!give(wk)) wk.terminate();
      while (idle.length > 0 && next < h && next + Math.min(ROWS, h - next) - written <= ring) {
        const v = idle.pop()!;
        if (!give(v)) v.terminate();
      }
      if (written >= h) for (const v of idle) v.terminate();
    });
    wk.postMessage(image); // the shared rows first, then one order at a time
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
    const ring: int = Math.trunc(image.length / bpr);
    for (let r = 0; r < rows; r++) row(y0 + r, n, n, image, ((y0 + r) % ring) * bpr, cr);
    parentPort.postMessage(m);
  });
}

if (isMainThread) mainThread(Math.trunc(Number(process.argv[2])));
else workerThread();
