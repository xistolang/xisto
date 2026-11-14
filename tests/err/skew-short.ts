// expect: runtime error: index out of bounds
// Gauss-Seidel rows skewed (emit.c, sk_emit_loop): a row shorter than the columns the stencil reads; the guard fails
// and the loop as written reports the index, uncaught
function seidel(n: int, A: number[][]): void {
  for (let i = 1; i <= n - 2; i++)
  for (let j = 1; j <= n - 2; j++) A[i][j] = (A[i - 1][j] + A[i][j - 1] + A[i][j + 1] + A[i + 1][j]) / 4.0;
}
const A: number[][] = [];
for (let i = 0; i < 40; i++) A.push(new Array<number>(40).fill(1.0));
A[25] = [1, 2, 3];
seidel(40, A);
console.log(A[1][1]);
