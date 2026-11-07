// loop versioning (emit.c, bce_version_guard): `for (let i = A; i < LIM; i++)` over arrays the body cannot
// resize runs without index checks when A >= 0 and LIM <= each array's length, and with them otherwise, so an
// out-of-bounds index is still an error, at the same place. Checked against XISTO_NO_BCE=1.
const N: int = 5;
const a: number[] = [1, 2, 3, 4, 5];
const b: number[] = [10, 20, 30, 40, 50];
let s = 0;
for (let i = 0; i < N; i++) s += a[i] * b[i];
console.log("dot", s);
for (let i = 0; i < N; i++) for (let j = i + 1; j < N; j++) s += a[i] - b[j];
console.log("pairs", s);

const short: number[] = [1, 2, 3];
try {
  for (let i = 0; i < N; i++) s += short[i]; // LIM above the length: the checked loop, and its error
} catch (e) {
  console.log("long limit caught:", e.message, s);
}
let start: int = -2;
try {
  for (let i = start; i < 3; i++) s += a[i]; // a negative start: checked too
} catch (e) {
  console.log("negative start caught:", e.message);
}
let lim: int = 4;
for (let i = 0; i < lim; i++) {
  a[i] = a[i] + b[i]; // writes, no resize: versioned
}
console.log(a.join(","), lim);
const grow: number[] = [1, 2];
for (let i = 0; i < 2; i++) grow.push(grow[i]); // a push in the body: never versioned
console.log(grow.join(","));

// affine indices: a[i + E], a[E - i], a[i - E], and i <= LIM; in bounds and out of them
const v: int[] = [0, 1, 2, 3, 4, 5, 6, 7];
const k: int = 3;
let acc: int = 0;
for (let i = 0; i < 5; i++) acc += v[i + k] - v[k + 4 - i];
for (let i = 2; i <= 6; i++) acc += v[i - 2] * v[7 - i];
const half: int = 4;
for (let i = 1; i < half; i++) {
  const t = v[i];
  v[i] = v[7 - i];
  v[7 - i] = t;
}
console.log("affine", acc, v.join(","));
try {
  for (let i = 0; i < 5; i++) acc += v[i + 4]; // reaches v[8]
} catch (e) {
  console.log("affine past the end caught:", e.message, acc);
}
try {
  for (let i = 0; i <= 3; i++) acc += v[2 - i]; // reaches v[-1]
} catch (e) {
  console.log("affine below zero caught:", e.message, acc);
}
