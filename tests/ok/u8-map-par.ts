// `for (let i = lo; i < hi; i++) D[i] = T[S[i]]` on byte arrays: split over threads when large and in bounds, in
// place or into another array; overlapping views at an offset and small ranges keep the loop's order
function fill(n: int, seed: int): Uint8Array {
  const a = new Uint8Array(n);
  let x = seed;
  for (let i = 0; i < n; i++) {
    x = (x * 1103 + 13) % 65521;
    a[i] = x & 255;
  }
  return a;
}
function digest(a: Uint8Array, lo: int, hi: int): int {
  let h: int = 0;
  for (let i = lo; i < hi; i++) h = (h * 31 + a[i]) % 1000000007;
  return h;
}
const T = new Uint8Array(256);
for (let b = 0; b < 256; b++) T[b] = (b * 7 + 3) & 255;

const n = 5000003;
const a = fill(n, 1);
for (let i = 0; i < n; i++) a[i] = T[a[i]]; // in place, whole
console.log(digest(a, 0, n));

const s = fill(n, 2);
const d = new Uint8Array(n);
for (let i = 17; i < n - 5; i++) d[i] = T[s[i]]; // into another array, a range
console.log(digest(d, 0, n), d[16], d[17], d[n - 6], d[n - 5]);

const big = fill(n + 64, 3);
const v1 = big.subarray(0, n);
const v2 = big.subarray(1, n + 1); // the same bytes, one apart: each step reads what the one before wrote
for (let i = 0; i < n; i++) v2[i] = T[v1[i]];
console.log(digest(big, 0, n + 64));

const small = fill(1000, 4);
for (let i = 0; i < 1000; i++) small[i] = T[small[i]];
console.log(digest(small, 0, 1000));
