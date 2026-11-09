/* The Computer Language Benchmarks Game
   https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

   Greg Buchholz's simple C #2 program transliterated by Isaac Gouy.

   (xisto: the same program with types, each row of the bitmap computed apart, at the same time, with
   xisto:parallel; node/mandelbrot-node-1.js is the original)
*/
import { parallelFrom } from "xisto:parallel";

const iter = 50, limit = 2.0;

const h: int = process.argv.length > 2 ? Math.trunc(Number(process.argv[2])) : 200, w = h; // (+argv[2] || 200: xisto has no truthiness of numbers)
const bytesPerRow = (w + 7) >> 3;

/* the bits of row y, 8 pixels per byte, the last byte padded with zeros */
function row(y: int): Uint8Array {
  const bits = new Uint8Array(bytesPerRow);
  let bit: int = 0, byte: int = 0, at: int = 0;
  const ci = 2 * y / h - 1;
  for (let x = 0; x < w; ++x) {
    let zr = 0.0, zi = 0.0, tr = 0.0, ti = 0.0;
    const cr = 2 * x / w - 1.5;
    for (let i = 0; i < iter && (tr + ti <= limit * limit); ++i) {
      zi = 2 * zr * zi + ci;
      zr = tr - ti + cr;
      tr = zr * zr;
      ti = zi * zi;
    }

    byte <<= 1;
    if (tr + ti <= limit * limit) byte |= 0x01;
    ++bit;

    if (bit === 8) {
      bits[at++] = byte;
      byte = 0;
      bit = 0;
    } else if (x === w - 1) {
      byte <<= 8 - w % 8;
      bits[at++] = byte;
      byte = 0;
      bit = 0;
    }
  }
  return bits;
}

process.stdout.write(`P4\n${w} ${h}\n`);
for (const bits of parallelFrom(h, (y) => row(y))) process.stdout.write(bits);
