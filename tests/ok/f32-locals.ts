// number locals that only hold floats (emit.c, f32v_compute) are C floats, and Math.fround(x op y) of floats is the
// float operation: the same results as Node, bit for bit (a double holds the exact result of one float operation,
// so rounding it to a float is the float operation's rounding)
function filter(n: int, src: Float32Array, dst: Float32Array, alpha: number): void {
  const a1 = Math.fround(1 - Math.exp(-alpha));
  const a2 = Math.fround(Math.fround(a1 * Math.fround(Math.exp(-alpha))) * (alpha - 1.0));
  const b1 = Math.fround(Math.pow(2.0, -alpha));
  let ym1 = 0.0;
  let ym2 = 0.0;
  let xm1 = 0.0;
  for (let j = 0; j < n; j++) {
    dst[j] = Math.fround(Math.fround(Math.fround(a1 * src[j]) + Math.fround(a2 * xm1)) + Math.fround(b1 * ym1)) + Math.fround(-0.5 * ym2);
    xm1 = src[j];
    ym2 = ym1;
    ym1 = dst[j];
  }
}
const n = 12;
const src = new Float32Array(n);
for (let j = 0; j < n; j++) src[j] = Math.sin(j * 0.7) * 1e3 + j / 3;
const dst = new Float32Array(n);
filter(n, src, dst, 0.25);
console.log(dst.join(" "));

function edges(): void {
  // float operations at the edges: a tie between two floats, overflow to Infinity, a subnormal, -0, NaN
  let x = Math.fround(1.0);
  let y = Math.fround(Math.pow(2, -24));
  console.log(Math.fround(x + y), Math.fround(x + y + y), Math.fround(x - y));
  const big = Math.fround(3.4e38);
  console.log(Math.fround(big * Math.fround(2)), Math.fround(big + big), Math.fround(-big * Math.fround(10)));
  const tiny = Math.fround(1.401298464324817e-45);
  console.log(Math.fround(tiny / Math.fround(2)), Math.fround(tiny * Math.fround(3)), 1 / Math.fround(-tiny / Math.fround(4)));
  let z = Math.fround(0);
  z = -z;
  console.log(1 / z, Math.fround(z * Math.fround(5)), 1 / Math.fround(z - z));
  const nan = Math.fround(NaN);
  console.log(Math.fround(nan + x), Math.fround(Math.fround(1) / Math.fround(3)), Math.fround(Math.fround(0.1) * Math.fround(3)));

  // a local that is not always a float stays a double
  let d = Math.fround(0.1);
  console.log(d);
  d = 0.1;
  console.log(d, Math.fround(d + d));
  let e = Math.fround(2.5);
  e += 0.1;
  console.log(e);
}
edges();
