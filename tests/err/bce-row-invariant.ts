// expect: runtime error: index out of bounds
// an index that does not move in the loop over j (A[i][k]) past its row: the versioned loop does not hide it
function matmul(n: int, m: int, p: int, C: number[][], A: number[][], B: number[][]): void {
  for (let i = 0; i < n; i++)
  for (let k = 0; k < m; k++)
  for (let j = 0; j < p; j++) C[i][j] += A[i][k] * B[k][j];
}
matmul(2, 3, 2, [[0, 0], [0, 0]], [[1, 1], [1, 1]], [[1, 1], [1, 1], [1, 1]]);
