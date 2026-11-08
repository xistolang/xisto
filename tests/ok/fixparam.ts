// typed-array parameters that every call passes with one length are indexed against that length (the checks
// go when the index range fits), and a call with another length, or a function used as a value, keeps them
const K: int = 6;
const T: int[] = [0, 1, 2, 3, 4, 5];

function sum4(a: Float64Array, base: int): number {
  let s = 0;
  for (let k = 0; k < 4; k++) s += a[base + k];
  return s;
}
function fill(a: Float64Array): void {
  for (let i = 0; i < 4 * K; i++) a[i] = i * 0.5;
}
function pick(a: Float64Array, q: int): number {
  return a[4 * T[q] + 3];
}
function inner(b: Uint8Array): int {
  let s: int = 0;
  for (let i = 0; i < 8; i++) s += b[i];
  return s;
}
function outer(b: Uint8Array): int {
  return inner(b) + b[7];
}
function anyLen(a: Float64Array, i: int): number {
  return a[i];
}
function run(): void {
  const a = new Float64Array(24);
  fill(a);
  let t = 0;
  for (let q = 0; q < K; q++) t += sum4(a, 4 * T[q]) + pick(a, q);
  console.log(t);
  const b = new Uint8Array(8);
  for (let i = 0; i < 8; i++) b[i] = i * 3;
  console.log(outer(b));
  const small = new Float64Array(3);
  small[2] = 7;
  console.log(anyLen(a, 23), anyLen(small, 2));
  const f = (x: Float64Array): number => x[0];
  console.log(f(a));
}
run();
