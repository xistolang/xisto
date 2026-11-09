// xisto:fractal: the same answers as the plain escape-time loop, in Node (reference) and in xisto
import { inMandelbrotSet, mandelbrotRow } from "xisto:fractal";

function plain(cr: number, ci: number, iterations: int, radius: number): boolean {
  let zr = 0.0, zi = 0.0, tr = 0.0, ti = 0.0;
  for (let i = 0; i < iterations && tr + ti <= radius * radius; i++) {
    zi = 2 * zr * zi + ci;
    zr = tr - ti + cr;
    tr = zr * zr;
    ti = zi * zi;
  }
  return tr + ti <= radius * radius;
}

function hex(bits: Uint8Array): string {
  let s = "";
  for (let k = 0; k < bits.length; k++) s += (bits[k] < 16 ? "0" : "") + bits[k].toString(16);
  return s;
}

console.log(inMandelbrotSet(-1, 0, { iterations: 50, radius: 2 }), inMandelbrotSet(1, 1, { iterations: 50, radius: 2 }));
console.log(inMandelbrotSet(0.25, 0, { iterations: 1000, radius: 2 }), inMandelbrotSet(0.26, 0, { iterations: 1000, radius: 2 }));

// rows of several widths (not multiples of 8), iteration counts (0 to 7, 50) and radii (below 2: every step tested),
// points far out (|c| > 2: every step tested) and on the edge of the set
let mismatches = 0, checked = 0;
const widths: int[] = [1, 7, 8, 9, 16, 23, 64];
const counts: int[] = [0, 1, 2, 3, 4, 5, 6, 7, 50];
const radii: number[] = [2, 1.5, 3, 0.5];
for (const w of widths) {
  for (const iterations of counts) {
    for (const radius of radii) {
      for (const span of [3.0, 6.0]) {
        const reals = new Float64Array(w);
        for (let x = 0; x < w; x++) reals[x] = span * Number(x) / Number(w) - span * 0.75;
        for (let y = 0; y < 9; y++) {
          const ci = span * Number(y) / 9 - span / 2;
          const bits = mandelbrotRow(reals, ci, { iterations, radius });
          for (let x = 0; x < w; x++) {
            const got = (bits[x >> 3] & (128 >> (x & 7))) !== 0;
            if (got !== plain(reals[x], ci, iterations, radius)) mismatches++;
            if (got !== inMandelbrotSet(reals[x], ci, { iterations, radius })) mismatches++;
            checked++;
          }
          if (w % 8 !== 0 && (bits[bits.length - 1] & (255 >> (w % 8))) !== 0) mismatches++; // the padding stays 0
        }
      }
    }
  }
}
console.log("points", checked, "mismatches", mismatches);

// a row of the benchmark's picture
const size = 64;
const row = new Float64Array(size);
for (let x = 0; x < size; x++) row[x] = 2 * x / size - 1.5;
for (let y = 28; y < 36; y++) console.log(hex(mandelbrotRow(row, 2 * y / size - 1, { iterations: 50, radius: 2 })));

try { inMandelbrotSet(0, 0, { iterations: -1, radius: 2 }); } catch (e) { console.log((e as Error).message); }
try { mandelbrotRow(row, 0, { iterations: 50, radius: 0 }); } catch (e) { console.log((e as Error).message); }
