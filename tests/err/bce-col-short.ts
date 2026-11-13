// expect: runtime error: index out of bounds
// columns (emit.c, bce_col_test): A[j][i] in a loop over j with one row shorter than i; the guard fails and the
// copy with the checks reports the index, uncaught
function mvt(n: int, x: number[], y: number[], A: number[][]): void {
  for (let i = 0; i < n; i++)
  for (let j = 0; j < n; j++) x[i] = x[i] + A[j][i] * y[j];
}
mvt(3, [0, 0, 0], [1, 1, 1], [[1, 1, 1], [1, 1], [1, 1, 1]]);
