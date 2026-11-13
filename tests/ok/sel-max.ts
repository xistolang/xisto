// a selection reduction on an Int32Array slot (emit.c, bce_sel_plan): t[i][j] = max(t[i][j], e) in a loop over k
// keeps the slot in a 64-bit local without the ToInt32 of each store; the last value is stored when it fits int32,
// else the loop runs again as it was (the wrapped values JavaScript stores). The forms: a leaf function, Math.max,
// Math.min, a conditional written in place; the guards: rows in increasing order and apart, T's row outside the rows
// the loop reads, T outside a block read
function max_score(s1: int, s2: int): int {
  return s1 >= s2 ? s1 : s2;
}
function nus(n: int, t: Int32Array[]): void {
  for (let i = n - 1; i >= 0; i--)
  for (let j = i + 1; j < n; j++)
  for (let k = i + 1; k < j; k++) t[i][j] = max_score(t[i][j], t[i][k] + t[k + 1][j]);
}
function nusMath(n: int, t: Int32Array[]): void {
  for (let i = n - 1; i >= 0; i--)
  for (let j = i + 1; j < n; j++)
  for (let k = i + 1; k < j; k++) t[i][j] = Math.max(t[i][j], t[i][k] + t[k + 1][j]);
}
function nusMin(n: int, t: Int32Array[]): void {
  for (let i = n - 1; i >= 0; i--)
  for (let j = i + 1; j < n; j++)
  for (let k = i + 1; k < j; k++) t[i][j] = Math.min(t[i][j], t[i][k] - t[k + 1][j]);
}
function nusCond(n: int, t: Int32Array[]): void {
  for (let i = n - 1; i >= 0; i--)
  for (let j = i + 1; j < n; j++)
  for (let k = i + 1; k < j; k++) t[i][j] = t[i][k] + t[k + 1][j] > t[i][j] ? t[i][k] + t[k + 1][j] : t[i][j];
}
// a row of another array: T's address tested against it
function rowMax(n: int, t: Int32Array[], v: Int32Array): void {
  for (let i = 0; i < n; i++)
  for (let k = 0; k < n; k++) t[i][0] = Math.max(t[i][0], v[k] * 3 - 5);
}
function make(n: int, seed: int, big: boolean): Int32Array[] {
  const t: Int32Array[] = [];
  for (let i = 0; i < n; i++) {
    const r = new Int32Array(n);
    for (let j = 0; j < n; j++) r[j] = big ? 2147483000 + ((i * 7 + j * 13 + seed) % 600) : ((i * 7 + j * 13 + seed) % 23) - 11;
    t.push(r);
  }
  return t;
}
function show(name: string, t: Int32Array[]): void {
  let h = 0;
  for (const r of t) for (let j = 0; j < r.length; j++) h = (h * 31 + r[j]) % 1000000007;
  console.log(name, h, t[0].join(","));
}
const n = 12;
let t = make(n, 1, false); nus(n, t); show("leaf", t);
t = make(n, 2, false); nusMath(n, t); show("math", t);
t = make(n, 3, false); nusMin(n, t); show("min", t);
t = make(n, 4, false); nusCond(n, t); show("cond", t);
// past int32: the sums wrap where the program stores them, the loop runs again as it was
t = make(n, 5, true); nus(n, t); show("wrap", t);
t = make(n, 6, true); nusMin(n, t); show("wrapmin", t);
// rows in decreasing order (made, then reversed): no promise of apart rows, the loop as it was
t = make(n, 7, false); t.reverse(); nus(n, t); show("reversed", t);
// T's row is also a row the loop reads (the same array twice): the loop as it was
t = make(n, 8, false); t[5] = t[2]; nus(n, t); show("samerow", t);
// rows as views on one buffer that overlap
const sab = new SharedArrayBuffer(4 * (n + 3) * n);
const ov: Int32Array[] = [];
for (let i = 0; i < n; i++) {
  const r = new Int32Array(sab, 4 * 3 * i, n);
  ov.push(r);
}
for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) ov[i][j] = ((i * 5 + j * 3) % 17) - 8;
nus(n, ov); show("overlap", ov);
const v = new Int32Array(n);
for (let k = 0; k < n; k++) v[k] = (k * 11) % 7;
t = make(n, 9, false); rowMax(n, t, v); show("row", t);
// v is a row of t: T can be in it
t = make(n, 10, false); rowMax(n, t, t[3]); show("rowself", t);
