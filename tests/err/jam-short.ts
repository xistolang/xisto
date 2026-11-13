// expect: runtime error: index out of bounds
// unroll-and-jam (emit.c, uj_emit_loop): a row of B shorter than the columns; the guard fails and the loop as it
// was reports the index, uncaught
function mm(ni: int, nj: int, nk: int, C: number[][], A: number[][], B: number[][]): void {
  for (let i = 0; i < ni; i++)
  for (let j = 0; j < nj; j++) {
    C[i][j] = 0.0;
    for (let k = 0; k < nk; k++) C[i][j] += A[i][k] * B[k][j];
  }
}
const B: number[][] = [];
for (let k = 0; k < 3; k++) B.push([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
B[1] = [1, 2, 3];
mm(2, 10, 3, [[0, 0, 0, 0, 0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]], [[1, 1, 1], [1, 1, 1]], B);
