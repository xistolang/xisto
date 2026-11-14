// a min-plus row update with the pivot hoisted (emit.c, fw_emit_loop; floyd-warshall): path[i][k] read once before
// the j loop when path[k][k] >= 0, so the write at j = k leaves it as it was; and the loop as written when the guard
// fails: a negative diagonal (path[i][k] does change at j = k), rows that are views on one buffer, a short row,
// sums past int32 (stored with ToInt32, as Node does)
function floyd(n: int, path: Int32Array[]): void {
  for (let k = 0; k < n; k++)
  for (let i = 0; i < n; i++)
  for (let j = 0; j < n; j++)
  path[i][j] = path[i][j] < path[i][k] + path[k][j] ? path[i][j] : path[i][k] + path[k][j];
}
function floydLe(n: int, path: Int32Array[]): void {
  for (let k = 0; k < n; k++)
  for (let i = 0; i < n; i++)
  for (let j = 0; j < n; j++)
  path[i][j] = path[i][j] <= path[k][j] + path[i][k] ? path[i][j] : path[k][j] + path[i][k];
}
function make(n: int, seed: int): Int32Array[] {
  const p: Int32Array[] = [];
  for (let i = 0; i < n; i++) {
    const r = new Int32Array(n);
    for (let j = 0; j < n; j++) r[j] = i === j ? 0 : ((i * seed + j * 7) % 13) + 1;
    p.push(r);
  }
  return p;
}
function sum(p: Int32Array[]): number {
  let s = 0;
  for (const r of p) for (let j = 0; j < r.length; j++) s = (s * 31 + r[j]) % 1000003;
  return s;
}
let p = make(37, 5);
floyd(37, p);
console.log("floyd", sum(p));
p = make(37, 3);
floydLe(37, p);
console.log("floyd <=", sum(p));
// a negative diagonal: the guard fails for that k
p = make(21, 4);
p[3][3] = -2;
p[7][7] = -1;
floyd(21, p);
console.log("negative", sum(p));
// views on one buffer: rows 2 and 5 overlap
const buf = new SharedArrayBuffer(4 * 40);
p = make(10, 6);
p[2] = new Int32Array(buf, 0, 10);
p[5] = new Int32Array(buf, 8, 10);
for (let j = 0; j < 10; j++) { p[2][j] = j + 1; }
floyd(10, p);
console.log("views", sum(p));
// sums past int32: stored with ToInt32
p = make(9, 2);
for (let j = 0; j < 9; j++) { p[0][j] = 2147483000; p[j][0] = 2147483000; }
p[0][0] = 0;
p[4][4] = 2000000000;
floyd(9, p);
console.log("wrap", sum(p), p[1][2], p[4][4]);
