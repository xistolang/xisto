// an index E - i - C or E - i + C in a counted loop (emit.c, bce_cands_e; durbin's r[k - i - 1]): proved by the
// loop's guard (E - C - last >= 0, E - C - first < length), and the loop as written when the guard fails (an index
// out of range must still read undefined and fail the same way as on Node)
function rev(k: int, r: number[], y: number[]): number {
  let s = 0.0;
  for (let i = 0; i < k; i++) s += r[k - i - 1] * y[i];
  return s;
}
function revPlus(k: int, lo: int, r: number[]): number {
  let s = 0.0;
  for (let i = lo; i < k; i++) s = s * 0.5 + r[k - i + 2];
  return s;
}
function mirror(k: int, y: number[], z: number[]): void {
  for (let i = 0; i < k; i++) z[i] = y[i] + 0.25 * y[k - i - 1];
}
const r: number[] = [];
const y: number[] = [];
for (let i = 0; i < 12; i++) { r.push((i * 7 % 5) / 3); y.push((i + 1) / 4); }
let t = 0.0;
for (let k = 1; k <= 12; k++) t = t * 0.5 + rev(k, r, y);
console.log("rev", t.toFixed(9));
console.log("revPlus", revPlus(9, 0, r).toFixed(9), revPlus(9, 3, r).toFixed(9));
const z = new Array<number>(12).fill(0);
mirror(12, y, z);
console.log("mirror", z.map((v) => v.toFixed(4)).join(","));
// the guard fails (k past the end of r) but no index out of range is read: the loop as written
function gated(k: int, r: number[], y: number[]): number {
  let s = 0.0;
  for (let i = 0; i < k; i++) if (y[i] > 1.0) s += r[k - i - 1];
  return s;
}
const w: number[] = [];
for (let i = 0; i < 20; i++) w.push(i < 10 ? 0.5 : 2.0);
console.log("gated", gated(20, r, w), gated(12, r, w));
