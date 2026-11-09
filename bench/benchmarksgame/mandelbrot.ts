/* The Computer Language Benchmarks Game
   https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

   Greg Buchholz's simple C #2 program transliterated by Isaac Gouy.

   (xisto: each row of the bitmap is one call to mandelbrotRow of xisto:fractal, the rows computed at the same
   time with xisto:parallel; node/mandelbrot-node-1.js is the original)
*/
import { parallelFrom } from "xisto:parallel";
import { mandelbrotRow } from "xisto:fractal";

const h: int = process.argv.length > 2 ? Math.trunc(Number(process.argv[2])) : 200, w = h;

const reals = new Float64Array(w);
for (let x = 0; x < w; x++) reals[x] = 2 * x / w - 1.5;

process.stdout.write(`P4\n${w} ${h}\n`);
const rows = parallelFrom(h, (y) => mandelbrotRow(reals, 2 * y / h - 1, { iterations: 50, radius: 2 }));
for (const bits of rows) process.stdout.write(bits);
