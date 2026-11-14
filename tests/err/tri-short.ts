// expect: runtime error: index out of bounds
// the rows of a triangular elimination in lanes (emit.c, tr_emit_loop): a row shorter than n; the guard fails and the
// loop as written reports the index, uncaught
function lu(n: int, A: number[][]): void {
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < i; j++) {
      for (let k = 0; k < j; k++) A[i][j] -= A[i][k] * A[k][j];
      A[i][j] /= A[j][j];
    }
  }
}
const A: number[][] = [];
for (let i = 0; i < 20; i++) A.push(new Array<number>(20).fill(1.0));
A[15] = [1, 2, 3];
lu(20, A);
console.log(A[1][1]);
