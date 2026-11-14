// Gauss-Seidel rows skewed (emit.c, sk_emit_loop): R rows at once, row i + r two columns behind row i + r - 1; each
// element the same sum of the same values (new above and to the left, old below and to the right). Rows left over,
// a 5-point stencil, i < H as the bound, and the guards: rows reversed (apart), a row twice
function seidel(tsteps: int, n: int, A: number[][]): void {
  for (let t = 0; t <= tsteps - 1; t++)
  for (let i = 1; i <= n - 2; i++)
  for (let j = 1; j <= n - 2; j++)
  A[i][j] = (A[i - 1][j - 1] + A[i - 1][j] + A[i - 1][j + 1] + A[i][j - 1] + A[i][j] + A[i][j + 1] + A[i + 1][j - 1] + A[i + 1][j] +
    A[i + 1][j + 1]) / 9.0;
}
function five(n: int, m: int, w: number, A: number[][]): void {
  for (let i = 1; i < n - 1; i++)
  for (let j = 2; j < m - 1; j++)
  A[i][j] = w * (A[i - 1][j] + A[i + 1][j]) - (1.0 - w) * (A[i][j - 1] - A[i][j + 1]) + A[i][j] / 3.0;
}
function grid(n: int, m: int): number[][] {
  const g: number[][] = [];
  for (let i = 0; i < n; i++) {
    const r: number[] = [];
    for (let j = 0; j < m; j++) r.push((i * (j + 2) + 2) / n);
    g.push(r);
  }
  return g;
}
function sum(g: number[][]): number {
  let s = 0.0;
  for (const r of g) for (let j = 0; j < r.length; j++) s = s * 0.75 + r[j];
  return s;
}
const ns: int[] = [5, 12, 23, 40];
for (const n of ns) {
  const A = grid(n, n);
  seidel(3, n, A);
  console.log("seidel", n, sum(A), A[n >> 1].slice(0, 6).join(","));
}
const B = grid(30, 21);
five(30, 21, 0.3, B);
console.log("five", sum(B));
// rows made in one order, used in the other
const C = grid(30, 30);
C.reverse();
seidel(2, 30, C);
console.log("reversed", sum(C));
// a row twice: the loop as written
const D = grid(30, 30);
D[9] = D[7];
seidel(2, 30, D);
console.log("twice", sum(D));
