// a selection between an Int32Array element and a sum, in 32 bits (emit.c, i32sel_emit): the sum wraps with its
// overflow bit, the test comes out as in 64 bits, the stored value is ToInt32 of the pick; floyd-warshall's form, the
// other orders and tests, a difference, sums past int32 both ways, and the expression's value
function floyd(n: int, p: Int32Array[]): void {
  for (let k = 0; k < n; k++)
  for (let i = 0; i < n; i++)
  for (let j = 0; j < n; j++) p[i][j] = p[i][j] < p[i][k] + p[k][j] ? p[i][j] : p[i][k] + p[k][j];
}
function forms(a: Int32Array, b: Int32Array, c: Int32Array, out: Int32Array[]): number {
  let s = 0.0;
  for (let j = 0; j < a.length; j++) {
    out[0][j] = b[j] + c[j] > a[j] ? a[j] : b[j] + c[j];
    out[1][j] = a[j] >= b[j] + c[j] ? a[j] : b[j] + c[j];
    out[2][j] = b[j] - c[j] <= a[j] ? b[j] - c[j] : a[j];
    out[3][j] = a[j] > b[j] - c[j] ? b[j] - c[j] : a[j];
    s += (out[4][j] = a[j] < b[j] + c[j] ? a[j] : b[j] + c[j]) % 1000;
  }
  return s;
}
const n = 9;
const p: Int32Array[] = [];
for (let i = 0; i < n; i++) {
  const r = new Int32Array(n);
  for (let j = 0; j < n; j++) r[j] = i === j ? 0 : ((i * 5 + j * 3) % 7) * 10 + 3;
  p.push(r);
}
floyd(n, p);
console.log(p.map((r) => r.join(",")).join(" "));
const big = [2147483647, -2147483648, 2000000000, -2000000000, 5, -7, 1500000000, -1500000000, 0, 1];
const m = big.length * big.length;
const a = new Int32Array(m), b = new Int32Array(m), c = new Int32Array(m);
let q = 0;
for (let x = 0; x < big.length; x++)
for (let y = 0; y < big.length; y++) {
  a[q] = big[(x + y) % big.length];
  b[q] = big[x];
  c[q] = big[y];
  q++;
}
const out: Int32Array[] = [];
for (let k = 0; k < 5; k++) out.push(new Int32Array(m));
const s = forms(a, b, c, out);
console.log(s);
for (const r of out) console.log(r.join(","));
