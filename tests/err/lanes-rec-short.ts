// expect: runtime error: index out of bounds
// lanes of a loop with a recurrence (emit.c, lj_emit): a row of u shorter than the columns the sweep reads; the
// guard fails and the loop as written reports the index, uncaught
function sweep(n: int, u: number[][], p: number[][], q: number[][]): void {
  for (let i = 1; i < n - 1; i++) {
    p[i][0] = 0.0;
    q[i][0] = 1.0;
    for (let j = 1; j < n - 1; j++) {
      p[i][j] = -0.25 / (0.5 * p[i][j - 1] + 1.5);
      q[i][j] = (u[j][i - 1] + u[j][i + 1] - 0.5 * q[i][j - 1]) / (0.5 * p[i][j - 1] + 1.5);
    }
  }
}
const g = (s: int): number[][] => Array.from({ length: 10 }, (_, i) => Array.from({ length: 10 }, (_, j) => (i + j + s) / 10.0));
const u = g(1);
u[4] = [1, 2, 3];
sweep(10, u, g(2), g(3));
console.log(u[1][1]);
