// expect: runtime error: index out of bounds
// loop versioning on rows (emit.c, bce_version_guard): B[k] is shorter than the loop over j; the guard fails and
// the copy with the checks reports the out-of-bounds index uncaught
function matmul(n: int, m: int, p: int, C: number[][], A: number[][], B: number[][]): void {
  for (let i = 0; i < n; i++)
  for (let k = 0; k < m; k++)
  for (let j = 0; j < p; j++) C[i][j] += A[i][k] * B[k][j];
}
const C: number[][] = [[0, 0, 0], [0, 0, 0]];
const A: number[][] = [[1, 1], [1, 1]];
const B: number[][] = [[1, 2, 3], [4, 5]];
matmul(2, 2, 3, C, A, B);
console.log(C[0].join());
