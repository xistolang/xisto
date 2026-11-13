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

// the sum in a local (ludcmp): w = T; for k w -= f; T = w; w after the loop is the last j's
function ludRest(n: int, A: number[][]): number {
  let w: number = 0;
  for (let i = 0; i < n; i++)
  for (let j = i; j < n; j++) {
    w = A[i][j];
    for (let k = 0; k < i; k++) w -= A[i][k] * A[k][j];
    A[i][j] = w;
  }
  return w;
}
const W = grid(12, 12, (i, j) => ((i * 7 + j * 5) % 13) / 9 + (i === j ? 3 : 0));
console.log("ludcmp rest", ludRest(12, W).toFixed(9), show(W));

// the k form (syrk): for k { for j <= i: C[i][j] += alpha * A[i][k] * A[j][k] }, U values of k into each C[i][j]
function syrk(n: int, m: int, alpha: number, beta: number, C: number[][], A: number[][]): void {
  for (let i = 0; i < n; i++) {
    for (let j = 0; j <= i; j++) C[i][j] *= beta;
    for (let k = 0; k < m; k++) for (let j = 0; j <= i; j++) C[i][j] += alpha * A[i][k] * A[j][k];
  }
}
const SC = grid(7, 7, (i, j) => (i + j) / 10);
syrk(7, 19, 1.5, 0.5, SC, grid(7, 19, (i, j) => ((i * 4 + j) % 9) / 8));
console.log("syrk", show(SC));
// a row of C is a row of A: the old loop
const SA2 = grid(7, 19, (i, j) => ((i + j * 2) % 7) / 6);
const SC2 = grid(7, 7, (i, j) => 1);
SA2[3] = SC2[5];
for (let r = 0; r < 7; r++) if (SA2[r].length < 19) for (let c = SA2[r].length; c < 19; c++) SA2[r].push(c / 19);
syrk(7, 19, 0.5, 2, SC2, SA2);
console.log("syrk shared row", show(SC2));

// a statement on T after the sum (trmm): B[i][j] = alpha * B[i][j]
function trmm(m: int, n: int, alpha: number, A: number[][], B: number[][]): void {
  for (let i = 0; i < m; i++)
  for (let j = 0; j < n; j++) {
    for (let k = i + 1; k < m; k++) B[i][j] += A[k][i] * B[k][j];
    B[i][j] = alpha * B[i][j];
  }
}
const TB = grid(6, 17, (i, j) => ((i * 5 + j) % 7) / 3);
trmm(6, 17, 1.25, grid(6, 6, (i, j) => ((i + j) % 4) / 2), TB);
console.log("trmm", show(TB));
const TB2 = grid(6, 17, (i, j) => ((i + 3 * j) % 5) / 4);
TB2[4] = TB2[1];
trmm(6, 17, 0.75, grid(6, 6, (i, j) => (i * j + 1) / 7), TB2);
console.log("trmm shared rows", show(TB2));

// T /= g after the sum, and the mirror Z[j][c] = T (covariance); the mean's loop: mean[j] /= n
function cov(m: int, n: int, fn: number, data: number[][], C: number[][], mean: number[]): void {
  for (let j = 0; j < m; j++) {
    mean[j] = 0.0;
    for (let i = 0; i < n; i++) mean[j] += data[i][j];
    mean[j] /= fn;
  }
  for (let i = 0; i < m; i++)
  for (let j = i; j < m; j++) {
    C[i][j] = 0.0;
    for (let k = 0; k < n; k++) C[i][j] += data[k][i] * data[k][j];
    C[i][j] /= fn - 1.0;
    C[j][i] = C[i][j];
  }
}
const CD = grid(11, 14, (i, j) => ((i * 3 + j * 7) % 10) / 4);
const CC = grid(14, 14, (i, j) => 0);
const CM: number[] = new Array<number>(14).fill(0);
cov(14, 11, 11, CD, CC, CM);
console.log("cov", show(CC), CM.map((x) => x.toFixed(6)).join(","));
// the data's rows are the matrix's: the old loop
const CC2 = grid(14, 14, (i, j) => ((i + j) % 3) / 2);
cov(14, 11, 11, CC2, CC2, new Array<number>(14).fill(0));
console.log("cov shared", show(CC2));

// column writes in the k loop and a sum in a local (symm): C[k][j] += ..., temp2 += ..., then C[i][j] = g(C[i][j], temp2)
function symm(m: int, n: int, alpha: number, beta: number, C: number[][], A: number[][], B: number[][]): void {
  let temp2: number;
  for (let i = 0; i < m; i++)
  for (let j = 0; j < n; j++) {
    temp2 = 0;
    for (let k = 0; k < i; k++) {
      C[k][j] += alpha * B[i][j] * A[i][k];
      temp2 += B[k][j] * A[i][k];
    }
    C[i][j] = beta * C[i][j] + alpha * B[i][j] * A[i][i] + alpha * temp2;
  }
}
const YA = grid(7, 7, (i, j) => ((i * 2 + j) % 5) / 3);
const YB = grid(7, 18, (i, j) => ((i + j * 3) % 7) / 5);
const YC = grid(7, 18, (i, j) => ((i * j) % 4) / 2);
symm(7, 18, 1.5, 1.2, YC, YA, YB);
console.log("symm", show(YC));
// B is C: the column read is the column written, in the same order per lane
const YC2 = grid(7, 18, (i, j) => ((i + 2 * j) % 6) / 4);
symm(7, 18, 0.5, 0.75, YC2, YA, YC2);
console.log("symm B = C", show(YC2));

// x[i] = x[i] + A[i][j] * y[j]: the lane's own row (mvt), and x[i] = x[i] + A[j][i] * y[j]: a column
function mvt(n: int, x1: number[], x2: number[], y1: number[], y2: number[], A: number[][]): void {
  for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) x1[i] = x1[i] + A[i][j] * y1[j];
  for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) x2[i] = x2[i] + A[j][i] * y2[j];
}
const MA = grid(13, 13, (i, j) => ((i * 5 + j) % 9) / 7);
const mx1: number[] = new Array<number>(13).fill(0.5);
const mx2: number[] = new Array<number>(13).fill(0.25);
const my: number[] = [];
for (let j = 0; j < 13; j++) my.push((j % 4) / 3);
mvt(13, mx1, mx2, my, my, MA);
console.log("mvt", mx1.map((x) => x.toFixed(6)).join(","), mx2.map((x) => x.toFixed(6)).join(","));
// x is a row of A: the old loop
mvt(13, MA[2], MA[5], my, MA[2], MA);
console.log("mvt shared", show(MA));
