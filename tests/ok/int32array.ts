// Int32Array with direct indexes, as Node: 4 bytes per element; a read is the int, a write is ToInt32 of the value
// (an int wrapped modulo 2^32, a number truncated toward 0 first, NaN and the Infinities 0).

const a = new Int32Array(4);
console.log(a, a.length, a.byteLength, a.byteOffset);
a[0] = 7;
a[1] = -3;
a[2] = 2147483647;
a[3] = a[2] + 1; // wraps to -2^31
console.log(a, a[0] + a[1], a[3]);

// writes through ToInt32
const w = new Int32Array(12);
w[0] = 4294967296; // 2^32: 0
w[1] = 4294967297; // 1
w[2] = -2147483649; // 2^31 - 1
w[3] = 3000000000;
w[4] = 1.9;
w[5] = -1.9;
w[6] = NaN;
w[7] = Infinity;
w[8] = -Infinity;
w[9] = 1e20;
w[10] = -1e20;
w[11] = 9007199254740991;
console.log(w);
const z = new Int32Array(1);
z[0] = -0.5;
console.log(z[0], 1 / z[0]);

// the value of an assignment is the value computed, not the element
const v = new Int32Array(3);
console.log((v[0] = 3000000000), v[0], (v[1] = 2.75), v[1]);
// compound assignments and ++ / --
v[2] = 2147483647;
v[2] += 1;
console.log(v[2]);
v[2] -= 1;
console.log(v[2]);
v[0] = 100;
v[0] *= 3;
v[0] /= 7;
console.log(v[0]);
v[1] = 5;
v[1] += 0.9;
console.log(v[1]);
let old = v[2]++;
console.log(old, v[2]);
old = v[2]--;
console.log(old, v[2]);
v[1] = 65535;
v[1] *= 65537;
console.log(v[1]);

// constructors: a length, int[], number[], an Int32Array, a SharedArrayBuffer
const fromInts = new Int32Array([1, -2, 3]);
const fromNums = new Int32Array([1.5, -2.5, 4294967295, NaN]);
const copy = new Int32Array(fromInts);
copy[0] = 99;
console.log(fromInts, fromNums, copy);
const sab = new SharedArrayBuffer(16);
const view = new Int32Array(sab, 4, 2);
const whole = new Int32Array(sab);
view[0] = 5;
view[1] = -6;
console.log(whole, view.byteOffset, view.byteLength);

// fill, set, subarray (a view), slice (a copy), join, toString
const f = new Int32Array(6);
f.fill(7);
console.log(f.join("-"));
f.fill(-1.5, 2, 4);
console.log(f.toString());
f.fill(4294967296 + 9, -1);
console.log(f);
f.set([10, 20], 1);
f.set(new Int32Array([30, 40]), 3);
const nums: number[] = [2.5, -3e9];
f.set(nums, 4);
console.log(f);
const sub = f.subarray(1, 3);
sub[0] = 123;
const sl = f.slice(2, 5);
sl[0] = 456;
console.log(f, sub, sl, sub.byteOffset);

// for...of, and a sum in an int
let sum = 0;
for (const x of fromInts) sum += x;
console.log("sum", sum);

// a loop over rows of Int32Array (Floyd-Warshall's shape)
const n = 5;
const path: Int32Array[] = [];
for (let i = 0; i < n; i++) {
  const row = new Int32Array(n);
  for (let j = 0; j < n; j++) row[j] = (i + j) % 3 === 0 ? 999 : ((i * j) % 7) + 1;
  path.push(row);
}
for (let k = 0; k < n; k++)
for (let i = 0; i < n; i++)
for (let j = 0; j < n; j++)
path[i][j] = path[i][j] < path[i][k] + path[k][j] ? path[i][j] : path[i][k] + path[k][j];
console.log(path.map((r) => r.join(",")).join(" "));
