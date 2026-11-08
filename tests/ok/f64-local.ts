// a small Float64Array a function only indexes lives in its frame (no heap object); anything that lets it
// escape keeps the heap object. Both paths give the same answers (and XISTO_NO_F64_LOCAL=1 too).
const K: int = 4;

function dot(a: Float64Array, b: Float64Array): number {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * b[i];
  return s;
}
function bump(a: Float64Array, i: int): void {
  a[i] += 1.5;
  a[(i + 1) % a.length] -= a[i] * 2;
}
// promoted: only indexed, only handed to functions that only index their parameter
function promoted(n: int): number {
  const u = new Float64Array(K), w = new Float64Array(3);
  for (let i = 0; i < K; i++) u[i] = i + 0.5;
  w[0] = 2; w[1] = 4; w[2] = 8;
  let t = 0;
  for (let k = 0; k < n; k++) {
    bump(u, k % K);
    t += dot(u, u) - w[k % 3] + Number(u.length);
  }
  return t;
}
// aliasing between two locals handed to the same function: the same object twice
function aliased(): number {
  const v = new Float64Array(3);
  v[0] = 1; v[1] = 2; v[2] = 3;
  bump(v, 2);
  return dot(v, v);
}
// escapes: returned, stored in a field, captured, iterated, a method called, passed to a function that stores it
class Box { a: Float64Array | null = null; }
function keep(a: Float64Array, b: Box): void { b.a = a; }
function returned(): Float64Array {
  const r = new Float64Array(2);
  r[0] = 7; r[1] = 9;
  return r;
}
function escapes(): number {
  const box = new Box();
  const s = new Float64Array(2);
  s[0] = 3; s[1] = 5;
  keep(s, box);
  const c = new Float64Array(2);
  c[1] = 11;
  const f = () => c[1] + c[0];
  const it = new Float64Array(3);
  it[0] = 1; it[1] = 2; it[2] = 4;
  let sum = 0;
  for (const x of it) sum += x;
  const m = new Float64Array(2);
  m.fill(0.5);
  const r = returned();
  const held = box.a;
  return (held === null ? 0 : held[1]) + f() + sum + m[0] + m[1] + r[0] * r[1];
}
console.log(promoted(1), promoted(7), promoted(100));
console.log(aliased());
console.log(escapes());
