// columns (emit.c, bce_rect_open, bce_col_test): X[k][c] in a loop over k, c invariant, is proved by the loop's
// guard when every row X[k] it reaches is longer than c: the least row length of X, computed once for the loop
// nest, or the rows of the range one by one when the range is short. A short row is still an error, at the same
// element. Checked against XISTO_NO_BCE=1.
function grid(r: int, c: int, f: (i: int, j: int) => number): number[][] {
  const g: number[][] = [];
  for (let i = 0; i < r; i++) {
    const row: number[] = [];
    for (let j = 0; j < c; j++) row.push(f(i, j));
    g.push(row);
  }
  return g;
}
function show(g: number[][]): string {
  return g.map((r) => r.map((x) => x.toFixed(3)).join(",")).join(" ");
}
function mvt(n: int, x2: number[], y2: number[], A: number[][]): void {
  for (let i = 0; i < n; i++)
  for (let j = 0; j < n; j++)
  x2[i] = x2[i] + A[j][i] * y2[j];
}
function trmm(m: int, n: int, alpha: number, A: number[][], B: number[][]): void {
  for (let i = 0; i < m; i++)
  for (let j = 0; j < n; j++) {
    for (let k = i + 1; k < m; k++)
    B[i][j] += A[k][i] * B[k][j];
    B[i][j] = alpha * B[i][j];
  }
}
// a few rows of a tall array: the range is short, so its rows are tested one by one
function band(A: number[][], from: int, to: int, c: int): number {
  let s = 0;
  for (let k = from; k < to; k++) s += A[k][c];
  return s;
}

const A = grid(5, 5, (i, j) => ((i * j + 1) % 5) / 5);
const x2 = [0, 0, 0, 0, 0];
mvt(5, x2, [1, 2, 3, 4, 5], A);
console.log("mvt", x2.map((x) => x.toFixed(4)).join(" "));
const TA = grid(4, 4, (i, j) => (i + j) / 4);
const TB = grid(4, 3, (i, j) => (i * 3 + j) / 7);
trmm(4, 3, 1.5, TA, TB);
console.log("trmm", show(TB));

// a ragged array: a long row, then short ones; the columns past the short rows fail where the plain loop fails
const tall = grid(40, 3, (i, j) => i + j);
tall[30] = [1];
console.log("band before the short row", band(tall, 2, 5, 2));
console.log("band over all rows, column 0", band(tall, 0, 40, 0));
try {
  console.log(band(tall, 28, 32, 2));
} catch (e) {
  console.log("short row in a short range caught:", e.message);
}
try {
  console.log(band(tall, 0, 40, 1));
} catch (e) {
  console.log("short row in the whole range caught:", e.message);
}
try {
  console.log(band(tall, 0, 3, -1));
} catch (e) {
  console.log("negative column caught:", e.message);
}
try {
  console.log(band(tall, 38, 41, 0));
} catch (e) {
  console.log("row past the array caught:", e.message);
}
const ragged = grid(5, 5, (i, j) => 1);
ragged[3] = [1, 1];
const rx = [0, 0, 0, 0, 0];
try {
  mvt(5, rx, [1, 1, 1, 1, 1], ragged);
} catch (e) {
  console.log("mvt short row caught:", e.message, rx.join(","));
}

// holes (new Array(n)): length 0 for the least length; the loop that never reaches them still runs fast or checked,
// with the same result
const H: number[][] = new Array<number[]>(6);
for (let i = 0; i < 4; i++) H[i] = [i, i + 1, i + 2];
console.log("holes, rows before them", band(H, 0, 4, 2));

// a nest that replaces a row or grows one: no least length, the same results
const W = grid(4, 3, (i, j) => i * 3 + j);
let ws = 0;
for (let i = 0; i < 3; i++) {
  for (let k = 0; k < 4; k++) ws += W[k][i];
  if (i === 0) W[2] = [7, 7, 7];
}
console.log("replaced", ws);
const G = grid(3, 2, (i, j) => 1);
let gs = 0;
for (let i = 0; i < 3; i++) {
  for (let k = 0; k < 3; k++) gs += G[k][i];
  for (let k = 0; k < 3; k++) G[k].push(k);
}
console.log("grown", gs);

// B = A * A^T, the init of lu and cholesky: A[s][t] in a loop over s
function aat(n: int, A: number[][]): number[][] {
  const B: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  for (let t = 0; t < n; ++t)
  for (let r = 0; r < n; ++r)
  for (let s = 0; s < n; ++s)
  B[r][s] += A[r][t] * A[s][t];
  return B;
}
console.log("aat", show(aat(4, grid(4, 4, (i, j) => ((i + 2 * j) % 5) / 5))));
