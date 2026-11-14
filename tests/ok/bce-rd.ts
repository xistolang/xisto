// columns through the rows' data pointers (emit.c, bce_rd_type): X[k][c] in a proved loop reads the table the guard
// built for the nest, for rows of number[], int[], Float32Array and Int32Array; a nest left early (return, break)
// frees the table as it ends
function colsum(n: int, X: number[][], c: int): number {
  let s = 0;
  for (let i = 0; i < 3; i++) for (let k = 0; k < n; k++) s += X[k][c] * (i + 1);
  return s;
}
function firstNeg(n: int, X: Float32Array[]): int {
  for (let c = 0; c < n; c++)
  for (let k = 0; k < n; k++)
  if (X[k][c] < 0) return c * 100 + k;
  return -1;
}
function maxCol(n: int, X: Int32Array[]): int {
  let best: int = 0;
  for (let c = 0; c < n; c++) {
    let s: int = 0;
    for (let k = 0; k < n; k++) s += X[k][c];
    if (s > best) best = s;
    if (s > 1000) break;
  }
  return best;
}
function intCols(n: int, X: int[][]): int {
  let t: int = 0;
  for (let c = 0; c < n; c++) for (let k = 0; k < n; k++) t += X[k][c] * c;
  return t;
}
const n = 70;
const A: number[][] = [];
const F: Float32Array[] = [];
const I: Int32Array[] = [];
const J: int[][] = [];
for (let k = 0; k < n; k++) {
  const a: number[] = [];
  const f = new Float32Array(n);
  const g = new Int32Array(n);
  const h: int[] = [];
  for (let c = 0; c < n; c++) {
    a.push((k * 7 + c * 3) % 11 / 4);
    f[c] = k === 50 && c === 33 ? -1.5 : (k + c) / 9;
    g[c] = (k * c) % 13;
    h.push((k + 2 * c) % 5);
  }
  A.push(a);
  F.push(f);
  I.push(g);
  J.push(h);
}
for (let r = 0; r < 3; r++) console.log(colsum(n, A, r * 20), firstNeg(n, F), maxCol(n, I), intCols(n, J));
