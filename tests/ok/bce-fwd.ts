// the value a loop carries (emit.c, bce_fw_plan): X[..][j - 1] read one iteration after X[..][j] was written is kept in a
// local; when another store of the body could replace it (a row of p that is q's), the loop reads memory as before
function sweep(n: int, a: number, b: number, c: number, p: number[][], q: number[][], u: number[][], v: number[][]): void {
  for (let i = 1; i < n - 1; i++) {
    p[i][0] = 0.0;
    q[i][0] = 1.0;
    for (let j = 1; j < n - 1; j++) {
      p[i][j] = -c / (a * p[i][j - 1] + b);
      q[i][j] = (u[j][i - 1] - a * q[i][j - 1]) / (a * p[i][j - 1] + b);
    }
    v[n - 1][i] = 1.0;
    for (let j = n - 2; j >= 1; j--) v[j][i] = p[i][j] * v[j + 1][i] + q[i][j];
  }
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
function show(g: number[][]): string {
  return g.map((r) => r.map((x) => x.toFixed(6)).join(",")).join(" ");
}
const n = 9;
const P = grid(n, n, (i, j) => 0), Q = grid(n, n, (i, j) => 0), V = grid(n, n, (i, j) => 0);
sweep(n, -0.5, 2.0, -0.5, P, Q, grid(n, n, (i, j) => ((i * 3 + j) % 7) / 5), V);
console.log("sweep", show(P), show(Q), show(V));
// p's rows are q's: the store to q[i][j] replaces p[i][j] before the next iteration reads it
const PQ = grid(n, n, (i, j) => 0);
const V2 = grid(n, n, (i, j) => 0);
sweep(n, -0.5, 2.0, -0.5, PQ, PQ, grid(n, n, (i, j) => ((i + 2 * j) % 5) / 3), V2);
console.log("sweep p = q", show(PQ), show(V2));
// a Gauss-Seidel row: A[i][j - 1] is the value just computed
const A = grid(7, 7, (i, j) => ((i * j) % 5) / 2);
for (let i = 1; i < 6; i++) for (let j = 1; j < 6; j++) A[i][j] = (A[i][j - 1] + A[i][j] + A[i - 1][j] + A[i + 1][j]) / 4.0;
console.log("seidel", show(A));
