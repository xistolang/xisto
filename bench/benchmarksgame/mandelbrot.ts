/* The Computer Language Benchmarks Game
   https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

   Greg Buchholz's simple C #2 program transliterated by Isaac Gouy.

   stdout NOT BUFFERED
   (xisto: the same program with types; node/mandelbrot-node-1.js is the original)
*/

let bit = 0;
const byte = new Uint8Array(1);
const iter = 50, limit = 2.0;
let zr = 0.0, zi = 0.0, cr = 0.0, ci = 0.0, tr = 0.0, ti = 0.0;

const h = process.argv.length > 2 ? Number(process.argv[2]) : 200, w = h; // (+argv[2] || 200: xisto has no truthiness of numbers)

process.stdout.write(`P4\n${w} ${h}\n`);

for (let y = 0; y < h; ++y) {
  for (let x = 0; x < w; ++x) {
    zr = zi = tr = ti = 0;
    cr = 2 * x / w - 1.5;
    ci = 2 * y / h - 1;
    for (let i = 0; i < iter && (tr + ti <= limit * limit); ++i) {
      zi = 2 * zr * zi + ci;
      zr = tr - ti + cr;
      tr = zr * zr;
      ti = zi * zi;
    }

    byte[0] <<= 1;
    if (tr + ti <= limit * limit) byte[0] |= 0x01;
    ++bit;

    if (bit === 8) {
      process.stdout.write(byte);
      byte[0] = 0;
      bit = 0;
    } else if (x === w - 1) {
      byte[0] <<= 8 - w % 8;
      process.stdout.write(byte);
      byte[0] = 0;
      bit = 0;
    }
  }
}
