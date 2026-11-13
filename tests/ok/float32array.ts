// Float32Array and Math.fround, as Node: 4 bytes per element, a write rounds to the nearest float (ties to even),
// a read is that float as a number; the arithmetic in between stays in double.

const a = new Float32Array(3);
console.log(a, a.length, a.byteLength, a.byteOffset);
a[0] = 1.1;
a[1] = 0.1 + 0.2;
a[2] = 1 / 3;
console.log(a);
console.log(a[0], a[1], a[2]);
console.log(a[0] === Math.fround(1.1), a[0] === 1.1);

// rounding: ties to even, at 1 + 2^-24 (halfway between 1 and the next float)
const r = new Float32Array(6);
r[0] = 1 + Math.pow(2, -24);              // tie: to 1 (even)
r[1] = 1 + 3 * Math.pow(2, -24);          // tie: up to 1 + 2^-22 (even)
r[2] = 1 + Math.pow(2, -24) + Math.pow(2, -40); // past the tie: up
r[3] = 16777217;                          // 2^24 + 1: to 2^24
r[4] = 16777219;                          // 2^24 + 3: to 2^24 + 4
r[5] = -16777217;
console.log(r);

// overflow to Infinity, and the largest float
const o = new Float32Array(6);
o[0] = 3.4028234663852886e38;  // the largest float
o[1] = 3.4028235677973366e38;  // just under the halfway to 2^128: the largest float
o[2] = 3.4028235677973367e38;  // the halfway (ties to even: Infinity)
o[3] = 1e39;
o[4] = -1e39;
o[5] = Number.MAX_VALUE;
console.log(o);

// subnormals, underflow to a zero of its sign
const s = new Float32Array(7);
s[0] = 1.401298464324817e-45;  // the smallest subnormal
s[1] = 7.006492321624085e-46;  // half of it: a tie, to 0
s[2] = 7.006492321624087e-46;  // just over the half: up to the smallest
s[3] = -1e-46;                 // to -0
s[4] = 1.1754942106924411e-38; // the largest subnormal
s[5] = 1.1754943508222875e-38; // the smallest normal
s[6] = 5e-324;
console.log(s);
console.log(1 / s[3] === -Infinity, 1 / s[1] === Infinity, 1 / s[3]);

// -0, NaN, Infinity
const z = new Float32Array([-0, 0, NaN, Infinity, -Infinity, -0.0]);
console.log(z);
console.log(1 / z[0] === -Infinity, Number.isNaN(z[2]), z[3] === Infinity, z[4] === -Infinity);

// Math.fround
const xs: number[] = [0, -0, 1.1, 0.1, 1e39, -1e39, 1e-46, -1e-46, NaN, Infinity, 5.5, 16777217, 2.5e-45, 3.4028235677973367e38];
for (const x of xs) console.log(Math.fround(x), 1 / Math.fround(x) === -Infinity);

// conversions: from number[], int[], a Float64Array, a Float32Array; into a Float64Array
const fromNums = new Float32Array([0.1, 0.2, 0.3, 1e40, -2.5]);
const ints: number[] = [1, 2, 16777217, -16777219, 123456789];
const fromInts = new Float32Array([1, 2, 16777217, -16777219, 123456789]);
console.log(fromNums, fromInts, ints.length);
const d = new Float64Array([0.1, 1e-50, 1e50, -0]);
const fromF64 = new Float32Array(d);
console.log(fromF64);
const back = new Float64Array(fromNums);
console.log(back);
const copy = new Float32Array(fromNums);
copy[0] = 9;
console.log(copy[0], fromNums[0]);

// arithmetic in double: the product of two floats is exact in double, not rounded to a float
const p = new Float32Array([1.1, 1.3]);
const prod = p[0] * p[1];
console.log(prod, Math.fround(prod), prod === Math.fround(prod));
let acc = 0;
for (let i = 0; i < 1000; i++) acc += p[0];
console.log(acc);

// compound assignment: read as a double, computed in double, stored rounded
const c = new Float32Array(4);
c[0] = 0.1;
c[0] += 0.2;
c[1] = 1;
c[1] /= 3;
c[2] = 16777216;
c[2]++;
c[3] = 3;
c[3] -= 0.1;
c[3] *= 7;
console.log(c);
const v1 = (c[0] = 1.1);   // the value of an assignment: the number written, not the float
console.log(v1, c[0]);
let k = 0;
const post = c[2]++;
console.log(post, c[2], k);
k = c[1]--;
console.log(k, c[1]);

// fill, set, subarray, slice, join, toString
const f = new Float32Array(8);
f.fill(0.7);
console.log(f);
f.fill(-0, 2, 4);
f.fill(1e40, -2);
console.log(f);
f.set([1.1, 2.2], 1);
f.set(new Float32Array([5.5]));
f.set(new Float64Array([0.3]), 7);
console.log(f);
const sub = f.subarray(1, 4);
sub[0] = 100.1;
console.log(sub, f[1], sub.byteOffset, sub.byteLength);
const sl = f.slice(-3);
sl[0] = 42;
console.log(sl, f[5]);
console.log(f.join(" | "), f.toString());
console.log(`${new Float32Array([0.1, -0, 2]).toString()}`);

// for...of, spread
let sum = 0;
for (const x of new Float32Array([0.1, 0.2, 0.3])) sum += x;
console.log(sum);
const spread = [...new Float32Array([0.5, 0.1])];
console.log(spread);

// on a SharedArrayBuffer, with an offset and a length
const sab = new SharedArrayBuffer(32);
const whole = new Float32Array(sab);
const part = new Float32Array(sab, 8, 4);
part[0] = 2.2;
part[3] = -1e-45;
console.log(whole, part.length, part.byteOffset);
const f64 = new Float64Array(sab);
console.log(f64.length);
const bytes = new Uint8Array(sab, 8, 4);
console.log(bytes);

// a function taking a Float32Array (a kernel, as PolyBench's deriche)
function scale(xs: Float32Array, k: number): number {
  let t = 0;
  for (let i = 0; i < xs.length; i++) {
    xs[i] = xs[i] * k + 0.5;
    t += xs[i];
  }
  return t;
}
const big = new Float32Array(1000);
for (let i = 0; i < big.length; i++) big[i] = i * 0.001;
console.log(scale(big, 1.7), big[999], big[500]);
const one = new Float64Array(1);
console.log(ArrayBuffer.isView(big), ArrayBuffer.isView(one), big instanceof Float32Array, one instanceof Float64Array);

// console.log of a long one
const long = new Float32Array(120);
for (let i = 0; i < long.length; i++) long[i] = i / 7;
console.log(long);
console.log([new Float32Array([1.5]), new Float32Array(0)]);
console.log({ w: new Float32Array([0.1, 0.25]) });

// prefix ++/--, an index with a side effect, and ** and % written out
const q = new Float32Array([1.1, 5.5, 2, 3]);
const inc = ++q[0];
const dec = --q[3];
q[1] = q[1] % 2.1;
q[2] = q[2] ** 0.5;
let qi = 1;
q[qi++] = q[qi] + 0.1;
console.log(inc, dec, q, qi);
