// loop versioning on rows (emit.c, bce_version_guard): in `for (let j = A; j < LIM; j++)` the rows X[R] of an
// array of arrays, R invariant in the loop (an outer counter, i + 1), are tested once (R within X, LIM within
// X[R]), and so are the indexes that do not move (A[i][k] in a loop over j); the copy with the checks runs
// otherwise, so a short row is still an error, at the same place. Checked against XISTO_NO_BCE=1.
function matmul(n: int, m: int, p: int, C: number[][], A: number[][], B: number[][]): void {
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < p; j++) C[i][j] *= 0.5;
    for (let k = 0; k < m; k++)
    for (let j = 0; j < p; j++) C[i][j] += 1.5 * A[i][k] * B[k][j];
  }
}
function stencil(steps: int, n: int, A: number[][], B: number[][]): void {
  for (let t = 0; t < steps; t++) {
    for (let i = 1; i < n - 1; i++)
    for (let j = 1; j < n - 1; j++)
    B[i][j] = 0.2 * (A[i][j] + A[i][j - 1] + A[i][1 + j] + A[1 + i][j] + A[i - 1][j]);
    for (let i = 1; i < n - 1; i++)
    for (let j = 1; j < n - 1; j++)
    A[i][j] = 0.2 * (B[i][j] + B[i][j - 1] + B[i][1 + j] + B[1 + i][j] + B[i - 1][j]);
  }
}
function shortest(n: int, path: int[][]): void {
  for (let k = 0; k < n; k++)
  for (let i = 0; i < n; i++)
  for (let j = 0; j < n; j++)
  path[i][j] = path[i][j] < path[i][k] + path[k][j] ? path[i][j] : path[i][k] + path[k][j];
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
function sum(g: number[][]): number {
  let s = 0;
  for (const row of g) for (const x of row) s += x;
  return s;
}

const A = grid(6, 7, (i, j) => ((i * (j + 1)) % 7) / 7);
const B = grid(7, 5, (i, j) => ((i * (j + 2)) % 5) / 5);
const C = grid(6, 5, (i, j) => ((i * j + 1) % 6) / 6);
matmul(6, 7, 5, C, A, B);
console.log("matmul", sum(C).toFixed(6), C[5].map((x) => x.toFixed(3)).join(" "));

const S = grid(8, 8, (i, j) => (i * (j + 2) + 2) / 8);
const T = grid(8, 8, (i, j) => (i * (j + 3) + 3) / 8);
stencil(4, 8, S, T);
console.log("stencil", sum(S).toFixed(6), S[3].map((x) => x.toFixed(4)).join(" "));

const P: int[][] = [];
for (let i = 0; i < 9; i++) {
  const row: int[] = [];
  for (let j = 0; j < 9; j++) row.push((i + j) % 4 === 0 ? 999 : (i * j) % 7 + 1);
  P.push(row);
}
shortest(9, P);
console.log("shortest", P.map((r) => r.join(",")).join(" "));

// a short row: the guard fails, the checked copy reports the index where it was reached
const ragged = grid(4, 6, (i, j) => i + j);
ragged[2] = [1, 2, 3];
const R = grid(4, 6, (i, j) => 0);
try {
  matmul(4, 4, 6, R, grid(4, 4, (i, j) => 1), ragged);
} catch (e) {
  console.log("short row caught:", e.message, sum(R));
}
// a short row met at an invariant index (A[i][k] with k past the row)
const narrow = grid(3, 2, (i, j) => 1);
try {
  matmul(3, 3, 2, grid(3, 2, (i, j) => 0), narrow, grid(3, 2, (i, j) => 1));
} catch (e) {
  console.log("invariant index caught:", e.message);
}
// a row index outside the array (B[k] with k past B)
try {
  matmul(2, 4, 2, grid(2, 2, (i, j) => 0), grid(2, 4, (i, j) => 1), grid(3, 2, (i, j) => 1));
} catch (e) {
  console.log("row outside caught:", e.message);
}
// a stencil on a grid with one short row: the neighbor row A[1 + i] fails, where the plain loop fails
const SR = grid(6, 6, (i, j) => i);
SR[4] = [1, 1, 1];
try {
  stencil(1, 6, SR, grid(6, 6, (i, j) => 0));
} catch (e) {
  console.log("neighbor row caught:", e.message);
}

// a body that replaces a row or resizes one: never versioned, the same results
const W = grid(3, 4, (i, j) => i * 4 + j);
for (let i = 0; i < 3; i++)
for (let j = 0; j < 4; j++) {
  if (j === 2 && i === 1) W[i] = [9, 9, 9, 9, 9];
  W[i][j] += 1;
}
console.log("replaced", W.map((r) => r.join(",")).join(" "));
const G = grid(2, 2, (i, j) => 1);
for (let i = 0; i < 2; i++)
for (let j = 0; j < 3; j++) {
  if (j === 1) G[i].push(5);
  G[i][j] += j;
}
console.log("pushed", G.map((r) => r.join(",")).join(" "));

// the limit n - 1 is computed once, after the start: its overflow is still an error, before the body runs
let big: int = 9007199254740991;
let ran = 0;
try {
  for (let i = 0; i < big + 1; i++) ran++;
} catch (e) {
  console.log("limit overflow caught:", e.message, ran);
}
let lim2: int = 5;
for (let i = 0; i < lim2 - 1; i++) ran += i;
console.log("limit", ran);

// overflow inside a row loop stays an error, at the same element
const O: int[][] = [[1, 2, 3], [9007199254740990, 5, 6], [7, 8, 9]];
try {
  for (let i = 0; i < 3; i++)
  for (let j = 0; j < 3; j++) O[i][j] = O[i][j] + O[1][0];
} catch (e) {
  console.log("overflow caught:", e.message, O.map((r) => r.join(",")).join(" "));
}

// a product of counters is never -0 (both >= 0); one of a counter and a negative is, at 0
let q = 0;
for (let i = 0; i < 3; i++) for (let j = 0; j < 4; j++) q += (i * (j + 1) % 4) / 4;
console.log("products", q);
for (let i = 0; i < 2; i++) console.log("negzero", 1 / ((i * (i - 3)) % 5));

// rows of rows: A[r][q][p] of a number[][][] (each level tested once), and a short innermost row
function doit(nr: int, nq: int, np: int, A: number[][][], C4: number[][], sum: number[]): void {
  for (let r = 0; r < nr; r++)
  for (let q = 0; q < nq; q++) {
    for (let p = 0; p < np; p++) {
      sum[p] = 0.0;
      for (let s = 0; s < np; s++) sum[p] += A[r][q][s] * C4[s][p];
    }
    for (let p = 0; p < np; p++) A[r][q][p] = sum[p];
  }
}
const D: number[][][] = [];
for (let i = 0; i < 3; i++) D.push(grid(4, 5, (j, k) => ((i * j + k) % 5) / 5));
doit(3, 4, 5, D, grid(5, 5, (i, j) => ((i * j) % 5) / 5), [0, 0, 0, 0, 0]);
console.log("rows of rows", D.map((m) => sum(m).toFixed(4)).join(" "));
D[1][2] = [1, 2];
try {
  doit(3, 4, 5, D, grid(5, 5, (i, j) => 1), [0, 0, 0, 0, 0]);
} catch (e) {
  console.log("short inner row caught:", e.message, D.map((m) => sum(m).toFixed(4)).join(" "));
}

// a hole among the rows (new Array(n)): a guard that reads it sees length 0, so a loop that does not run over it
// still does not touch it
const H: number[][] = new Array<number[]>(3);
H[0] = [1, 2];
let hz = 0;
for (let i = 0; i < 3; i++) for (let j = 0; j < i - 2; j++) hz += H[i][j];
console.log("hole", hz);
