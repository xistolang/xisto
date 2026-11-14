// expect: runtime error: index out of bounds
// a selection reduction (emit.c, bce_sel_plan): a row shorter than the columns the loop reads; the guard fails and
// the loop with the checks reports the index, uncaught
function nus(n: int, t: Int32Array[]): void {
  for (let i = n - 1; i >= 0; i--)
  for (let j = i + 1; j < n; j++)
  for (let k = i + 1; k < j; k++) t[i][j] = Math.max(t[i][j], t[i][k] + t[k + 1][j]);
}
const t: Int32Array[] = [];
for (let i = 0; i < 8; i++) t.push(new Int32Array(8));
t[5] = new Int32Array(3);
nus(8, t);
console.log(t[0][7]);
