// an invariant slot in a register (emit.c, bce_promo_plan): in a versioned loop, x[i] or A[i][j] that the body
// writes stays in a C local for the whole loop, unless another slot the body reads or writes can be the same
// element: the same row through another name, a row of a column that is that row, an index equal to it. Every
// case here must print what Node prints.
function mvt(n: int, x: number[], y: number[], A: number[][]): void {
  for (let i = 0; i < n; i++)
  for (let j = 0; j < n; j++) x[i] = x[i] + A[j][i] * y[j];
}
function mv(n: int, x: number[], y: number[], A: number[][]): void {
  for (let i = 0; i < n; i++)
  for (let j = 0; j < n; j++) x[i] = x[i] + A[i][j] * y[j];
}
function lu(n: int, A: number[][]): void {
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < i; j++) {
      for (let k = 0; k < j; k++) A[i][j] -= A[i][k] * A[k][j];
      A[i][j] /= A[j][j];
    }
    for (let j = i; j < n; j++) for (let k = 0; k < i; k++) A[i][j] -= A[i][k] * A[k][j];
  }
}
// the slot and another invariant index of the same row: equal when p === q
function pair(n: int, r: number[], p: int, q: int): void {
  for (let k = 0; k < n; k++) r[p] = r[p] * 0.5 + r[q] + k;
}
// the counter's index on the slot's own array: the slot inside the range or outside it
function self(n: int, v: number[], c: int): void {
  for (let k = 0; k < n; k++) v[c] += v[k] * 0.25;
}
function grid(r: int, c: int, f: (i: int, j: int) => number): number[][] {
  const g: number[][] = [];
  for (let i = 0; i < r; i++) {
    const row: number[] = [];
    for (let j = 0; j < c; j++) row.push(f(i, j));
    g.push(row);
  }
  return g;
}
function show(v: number[]): string {
  return v.map((x) => x.toFixed(5)).join(",");
}

const A = grid(5, 5, (i, j) => ((i * 3 + j * 7) % 11) / 11);
const x = [1, 0, 0, 0, 0];
mvt(5, x, [1, 2, 3, 4, 5], A);
console.log("mvt", show(x));
// x is a row of A: A[j][i] reaches x[i] when j is that row
const B = grid(5, 5, (i, j) => ((i + j) % 4) / 4);
mvt(5, B[2], [1, 2, 3, 4, 5], B);
console.log("mvt x = a row of A", B.map(show).join(" "));
// x is y
const xy = [0.5, 0.25, 0.125, 1, 2];
mvt(5, xy, xy, grid(5, 5, (i, j) => (i + 1) / (j + 1)));
console.log("mvt x = y", show(xy));
// x is the row A[i] itself (the same row through another name)
const C = grid(5, 5, (i, j) => ((i * j) % 5) / 5 + 0.1);
mv(5, C[3], [1, 1, 1, 1, 1], C);
console.log("mv x = A[3]", C.map(show).join(" "));

const L = grid(6, 6, (i, j) => (j <= i ? ((-j % 6) + 6) / 6 + 1 : 0.5));
lu(6, L);
console.log("lu", L.map(show).join(" "));
// two rows that are one array: A[k] is A[i] for some k
const D = grid(6, 6, (i, j) => ((i * 5 + j) % 7) / 7 + 1);
D[4] = D[1];
lu(6, D);
console.log("lu shared row", D.map(show).join(" "));

const r1 = [1, 2, 3, 4];
pair(5, r1, 1, 2);
const r2 = [1, 2, 3, 4];
pair(5, r2, 1, 1);
console.log("pair", show(r1), show(r2));
const v1 = [1, 2, 3, 4, 5, 6];
self(4, v1, 5);
const v2 = [1, 2, 3, 4, 5, 6];
self(4, v2, 2);
console.log("self", show(v1), show(v2));
