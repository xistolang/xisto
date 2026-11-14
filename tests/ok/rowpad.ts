// rows of a matrix with padded blocks (runtime xs_rowpad, XS_ROWPAD on Linux): number[][] and Int32Array[] rows of
// 1 KB or more, walked down a column, grown by push after the padding (realloc of a padded block), sliced and
// filled; every value must be what Node prints
function grid(n: int, m: int): number[][] {
  const g: number[][] = [];
  for (let i = 0; i < n; i++) {
    const r = new Array<number>(m).fill(0);
    for (let j = 0; j < m; j++) r[j] = ((i * 7 + j * 3) % 11) / 4;
    g.push(r);
  }
  return g;
}
const n = 40, m = 300; // 2400 B rows
const A = grid(n, m);
let s = 0.0;
for (let j = 0; j < m; j++) for (let i = 1; i < n; i++) s = s * 0.5 + A[i][j] - A[i - 1][j];
console.log("column", s.toFixed(6));
for (let i = 0; i < n; i++) for (let k = 0; k < 50; k++) A[i].push(i + k);
let t = 0.0;
for (const r of A) t += r.length + r[r.length - 1] + r[m - 1];
console.log("grown", t);
const T: Int32Array[] = [];
for (let i = 0; i < 30; i++) {
  const r = new Int32Array(700);
  for (let j = 0; j < 700; j++) r[j] = (i * 31 + j * 17) % 101;
  T.push(r);
}
let mx = 0;
for (let j = 1; j < 700; j++) for (let k = 0; k < 29; k++) mx = Math.max(mx, T[k][j] + T[k + 1][j - 1]);
console.log("typed", mx, T[29].slice(690).join(","));
const z = new Float64Array(2000).fill(1.5);
console.log("fill", z[0] + z[1999], z.length);
