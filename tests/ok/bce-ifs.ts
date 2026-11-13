// the ifs around an index (emit.c, bce_facts): table[i + 1][j] under `if (i + 1 < n)` is within the table where it is
// read, so the loop is versioned on that bound; the guard of a loop inside reads the row only when the if holds (a
// gate), never past the table
function nus(n: int, t: Int32Array[]): void {
  for (let i = n - 1; i >= 0; i--)
  for (let j = i + 1; j < n; j++) {
    if (j - 1 >= 0) t[i][j] = Math.max(t[i][j], t[i][j - 1]);
    if (i + 1 < n) t[i][j] = Math.max(t[i][j], t[i + 1][j]);
    if (j - 1 >= 0 && i + 1 < n) t[i][j] = Math.max(t[i][j], t[i + 1][j - 1] + 1);
  }
}
const n = 9;
const t: Int32Array[] = [];
for (let i = 0; i < n; i++) {
  const r = new Int32Array(n);
  for (let j = 0; j < n; j++) r[j] = (i * 3 + j * 5) % 4;
  t.push(r);
}
nus(n, t);
console.log(t.map((r) => r.join(",")).join(" "));
// an index read both under the if and outside it: the loop's own bound only
const u: number[] = [1, 2, 3, 4, 5];
let s = 0;
for (let i = 0; i < 4; i++) {
  if (i + 1 < 4) s += u[i + 1];
  s += u[i + 1] * 2;
}
console.log("both", s);
