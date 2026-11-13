// unroll-and-jam of j (emit.c, uj_emit_loop): U columns of a product at once, each sum in the order of k; the
// guard falls back to the loop as it was when a row read is the row written, or an index leaves a row. Every case
// here must print what Node prints.
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
  return g.map((r) => r.map((x) => x.toFixed(6)).join(",")).join(" ");
}
function mm(ni: int, nj: int, nk: int, alpha: number, C: number[][], A: number[][], B: number[][]): void {
  for (let i = 0; i < ni; i++)
  for (let j = 0; j < nj; j++) {
    C[i][j] = 0.0;
    for (let k = 0; k < nk; ++k) C[i][j] += alpha * A[i][k] * B[k][j];
  }
}
function scaleAdd(ni: int, nj: int, nk: int, beta: number, D: number[][], T: number[][], C: number[][]): void {
  for (let i = 0; i < ni; i++)
  for (let j = 0; j < nj; j++) {
    D[i][j] *= beta;
    for (let k = 0; k < nk; ++k) D[i][j] += T[i][k] * C[k][j];
  }
}
function luRest(n: int, A: number[][]): void {
  for (let i = 0; i < n; i++)
  for (let j = i; j < n; j++) for (let k = 0; k < i; k++) A[i][j] -= A[i][k] * A[k][j];
}
function vecmat(np: int, a: number[], C4: number[][], sum: number[]): void {
  for (let p = 0; p < np; p++) {
    sum[p] = 0.0;
    for (let s = 0; s < np; s++) sum[p] += a[s] * C4[s][p];
  }
}

const A = grid(5, 7, (i, j) => ((i * 3 + j) % 5) / 4);
const B = grid(7, 19, (i, j) => ((i + 2 * j) % 7) / 3);
const C = grid(5, 19, (i, j) => 0);
mm(5, 19, 7, 1.5, C, A, B);
console.log("mm", show(C));
// the row written is a row read: C[1] is B[2]
const B2 = grid(7, 19, (i, j) => ((i * j) % 5) / 2);
const C2 = grid(5, 19, (i, j) => 1);
C2[1] = B2[2];
mm(5, 19, 7, 0.5, C2, A, B2);
console.log("mm row shared", show(C2), show(B2));
// D *= beta, then the sums
const D = grid(4, 11, (i, j) => (i + j) / 5);
scaleAdd(4, 11, 7, 1.25, D, grid(4, 7, (i, j) => (i * j + 1) / 6), B);
console.log("scale add", show(D));
// lu's second half: A[i][k] on the row written, with k < i <= j
const L = grid(12, 12, (i, j) => ((i * 5 + j * 3) % 11) / 7 + (i === j ? 2 : 0));
luRest(12, L);
console.log("lu rest", show(L));
// an empty k loop, and fewer columns than U
mm(3, 5, 0, 2.0, C, A, B);
console.log("empty k", show(C));
// a 1D result
const sum = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
vecmat(10, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10], grid(10, 10, (i, j) => ((i + j) % 4) / 3), sum);
console.log("vecmat", sum.map((x) => x.toFixed(6)).join(","));
// the 1D result is a row of the matrix read
const M = grid(10, 10, (i, j) => (i * 10 + j) / 50);
vecmat(10, [1, 1, 1, 1, 1, 1, 1, 1, 1, 1], M, M[3]);
console.log("vecmat shared", show(M));

// a post loop that writes the lane's column after the sum (gramschmidt): Q and A apart, Q the same as A, R's row a
// row of A
function gs(m: int, n: int, A: number[][], R: number[][], Q: number[][]): void {
  for (let k = 0; k < n; k++) {
    let nrm = 0.0;
    for (let i = 0; i < m; i++) nrm += A[i][k] * A[i][k];
    R[k][k] = Math.sqrt(nrm);
    for (let i = 0; i < m; i++) Q[i][k] = A[i][k] / R[k][k];
    for (let j = k + 1; j < n; j++) {
      R[k][j] = 0.0;
      for (let i = 0; i < m; i++) R[k][j] += Q[i][k] * A[i][j];
      for (let i = 0; i < m; i++) A[i][j] = A[i][j] - Q[i][k] * R[k][j];
    }
  }
}
const GA = grid(9, 13, (i, j) => (((i * j) % 9) + 1) / 9);
const GR = grid(13, 13, (i, j) => 0);
const GQ = grid(9, 13, (i, j) => 0);
gs(9, 13, GA, GR, GQ);
console.log("gs", show(GA), show(GR), show(GQ));
const SA = grid(9, 13, (i, j) => (((i + 2 * j) % 7) + 1) / 7);
gs(9, 13, SA, grid(13, 13, (i, j) => 0), SA);
console.log("gs Q = A", show(SA));
const TA = grid(9, 13, (i, j) => (((i * 3 + j) % 5) + 1) / 5);
const TR = grid(13, 13, (i, j) => 0);
TR[2] = TA[4];
gs(9, 13, TA, TR, grid(9, 13, (i, j) => 0));
console.log("gs R row = A row", show(TA), show(TR));
