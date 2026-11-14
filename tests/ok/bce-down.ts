// loops that count down (emit.c, bce_version_guard): `for (let j = h - 1; j >= 0; j--)` over a row is versioned like
// a loop that counts up, its guard on the least and the greatest value of j; a body that writes j keeps the checks
function back(w: int, h: int, src: Float32Array[], dst: Float32Array[]): void {
  for (let i = 0; i < w; i++) {
    let p1 = 0.0;
    let p2 = 0.0;
    for (let j = h - 1; j >= 0; j--) {
      dst[i][j] = Math.fround(0.5 * p1 + 0.25 * p2 + src[i][j]);
      p2 = p1;
      p1 = src[i][j];
    }
  }
}
function suffix(a: Int32Array, out: number[]): void {
  let s = 0;
  for (let k = a.length - 1; k > 0; --k) {
    s += a[k];
    out[k - 1] = s;
  }
}
const w = 3, h = 5;
const src: Float32Array[] = [];
const dst: Float32Array[] = [];
for (let i = 0; i < w; i++) {
  const r = new Float32Array(h);
  for (let j = 0; j < h; j++) r[j] = (i * h + j) / 7;
  src.push(r);
  dst.push(new Float32Array(h));
}
back(w, h, src, dst);
console.log(dst.map((r) => r.join(",")).join(" "));
const out = [0, 0, 0, 0, 0];
suffix(new Int32Array([5, 4, 3, 2, 1, 7]), out);
console.log(out.join(","));
let seen = "";
const xs = [1, 2, 3, 4, 5, 6, 7, 8];
for (let k = xs.length - 1; k >= 0; k--) {
  seen += String(xs[k]);
  if (k % 3 === 0) k -= 1;
}
console.log("written in the body", seen);
let t = 0;
for (let k = 4; k >= 2; k -= 1) t += xs[k];
console.log("k -= 1", t);
