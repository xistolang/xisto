// expect: runtime error: index out of bounds
// a stencil nest (emit.c, rs_emit_loop): a row of A shorter than the columns read; the guard fails and the loop as
// written reports the index, uncaught
function jacobi(n: int, A: number[][], B: number[][]): void {
  for (let i = 1; i < n - 1; i++)
  for (let j = 1; j < n - 1; j++) B[i][j] = 0.25 * (A[i][j - 1] + A[i][j + 1] + A[i - 1][j] + A[i + 1][j]);
}
const A: number[][] = [], B: number[][] = [];
for (let i = 0; i < 20; i++) {
  A.push(new Array<number>(20).fill(1.0));
  B.push(new Array<number>(20).fill(0.0));
}
A[11] = [1, 2, 3];
jacobi(20, A, B);
console.log(B[1][1]);
