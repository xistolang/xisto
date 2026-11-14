// lanes of a loop whose inner loops carry a recurrence (emit.c, lj_emit): U iterations of the outer loop run
// together, each with its own locals, in the order of each lane; the same values as the loop as written. adi's
// sweeps (a row written, a column written, carried p[i][j - 1]), deriche's passes (Float32Array, carried locals),
// the o's left over, and the guards: arrays that overlap (the same array twice), a row repeated, rows reversed
function sweeps(n: int, u: number[][], v: number[][], p: number[][], q: number[][]): void {
  const a = -0.25, b = 1.5, c = a, d = -0.125, e = 1.25, f = d;
  for (let t = 1; t <= 3; t++) {
    for (let i = 1; i < n - 1; i++) {
      v[0][i] = 1.0;
      p[i][0] = 0.0;
      q[i][0] = v[0][i];
      for (let j = 1; j < n - 1; j++) {
        p[i][j] = -c / (a * p[i][j - 1] + b);
        q[i][j] = (-d * u[j][i - 1] + (1.0 + 2.0 * d) * u[j][i] - f * u[j][i + 1] - a * q[i][j - 1]) / (a * p[i][j - 1] + b);
      }
      v[n - 1][i] = 1.0;
      for (let j = n - 2; j >= 1; j--) {
        v[j][i] = p[i][j] * v[j + 1][i] + q[i][j];
      }
    }
    for (let i = 1; i < n - 1; i++) {
      u[i][0] = 1.0;
      p[i][0] = 0.0;
      q[i][0] = u[i][0];
      for (let j = 1; j < n - 1; j++) {
        p[i][j] = -f / (d * p[i][j - 1] + e);
        q[i][j] = (-a * v[i - 1][j] + (1.0 + 2.0 * a) * v[i][j] - c * v[i + 1][j] - d * q[i][j - 1]) / (d * p[i][j - 1] + e);
      }
      u[i][n - 1] = 1.0;
      for (let j = n - 2; j >= 1; j--) {
        u[i][j] = p[i][j] * u[i][j + 1] + q[i][j];
      }
    }
  }
}
function passes(w: int, h: int, img: Float32Array[], y1: Float32Array[], y2: Float32Array[], k: number[]): void {
  const a1 = Math.fround(0.5), a2 = Math.fround(-0.25), b1 = Math.fround(0.75), b2 = Math.fround(-0.125);
  for (let i = 0; i < w; i++) {
    let ym1 = 0.0;
    let ym2 = 0.0;
    let xm1 = 0.0;
    for (let j = 0; j < h; j++) {
      y1[i][j] = Math.fround(Math.fround(Math.fround(a1 * img[i][j]) + Math.fround(a2 * xm1)) + Math.fround(b1 * ym1)) + Math.fround(b2 * ym2);
      xm1 = img[i][j];
      ym2 = ym1;
      ym1 = y1[i][j];
    }
  }
  for (let j = 0; j < h; j++) {
    let tp1 = 0.0;
    let yp1 = 0.0;
    for (let i = w - 1; i >= 0; i--) {
      y2[i][j] = Math.fround(Math.fround(a1 * tp1) + Math.fround(b1 * yp1)) + k[i];
      tp1 = y1[i][j];
      yp1 = y2[i][j];
    }
  }
}
function grid(n: int, m: int, s: int): number[][] {
  const g: number[][] = [];
  for (let i = 0; i < n; i++) {
    const r: number[] = [];
    for (let j = 0; j < m; j++) r.push(((i * 7 + j * 3 + s) % 11) / 10.0);
    g.push(r);
  }
  return g;
}
function fgrid(n: int, m: int, s: int): Float32Array[] {
  const g: Float32Array[] = [];
  for (let i = 0; i < n; i++) {
    const r = new Float32Array(m);
    for (let j = 0; j < m; j++) r[j] = ((i * 5 + j * 2 + s) % 13) / 7.0;
    g.push(r);
  }
  return g;
}
function sum(g: number[][]): number {
  let s = 0.0;
  for (const r of g) for (let j = 0; j < r.length; j++) s = s * 0.5 + r[j];
  return s;
}
function fsum(g: Float32Array[]): number {
  let s = 0.0;
  for (const r of g) for (let j = 0; j < r.length; j++) s = s * 0.5 + r[j];
  return s;
}
const n = 12;
let u = grid(n, n, 1), v = grid(n, n, 2), p = grid(n, n, 3), q = grid(n, n, 4);
sweeps(n, u, v, p, q);
console.log("adi", sum(u), sum(v), u[5].join(","));
// n - 2 = 9 rows: two groups of 4 and one left
u = grid(11, 11, 1); v = grid(11, 11, 2); p = grid(11, 11, 3); q = grid(11, 11, 4);
sweeps(11, u, v, p, q);
console.log("odd", sum(u), sum(v));
// p and q the same array: the loop as written
u = grid(n, n, 1); v = grid(n, n, 2); p = grid(n, n, 3);
sweeps(n, u, v, p, p);
console.log("pq", sum(u), sum(v), sum(p));
// u and v the same array
u = grid(n, n, 1); p = grid(n, n, 3); q = grid(n, n, 4);
sweeps(n, u, u, p, q);
console.log("uv", sum(u), sum(p));
// a row of p twice
u = grid(n, n, 1); v = grid(n, n, 2); p = grid(n, n, 3); q = grid(n, n, 4);
p[6] = p[3];
sweeps(n, u, v, p, q);
console.log("row", sum(u), sum(v));
// rows made in one order, used in the other
u = grid(n, n, 1); v = grid(n, n, 2); p = grid(n, n, 3); q = grid(n, n, 4);
p.reverse(); q.reverse(); v.reverse();
sweeps(n, u, v, p, q);
console.log("reversed", sum(u), sum(v));
const w = 9, h = 7;
const img = fgrid(w, h, 1), y1 = fgrid(w, h, 2), y2 = fgrid(w, h, 3);
const k: number[] = [];
for (let i = 0; i < w; i++) k.push(i / 8.0);
passes(w, h, img, y1, y2, k);
console.log("deriche", fsum(y1), fsum(y2), y2[4].join(","));
// y2 written over img's rows (the same array): the loop as written
const im2 = fgrid(w, h, 1), z1 = fgrid(w, h, 2);
passes(w, h, im2, z1, z1, k);
console.log("same", fsum(z1));
