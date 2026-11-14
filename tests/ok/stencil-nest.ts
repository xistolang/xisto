// a stencil nest without a carried value (emit.c, rs_emit_loop): the innermost loop on row pointers, told to the C
// compiler that its iterations are independent, after one guard (kept for the outermost loop when it changes no
// row): jacobi-2d, heat-3d, fdtd-2d's x = x - f; and the guards: the same array read and written, a row twice, rows
// reversed, a row replaced between steps (the guard again)
function jacobi(tsteps: int, n: int, A: number[][], B: number[][]): void {
  for (let t = 0; t < tsteps; t++) {
    for (let i = 1; i < n - 1; i++)
    for (let j = 1; j < n - 1; j++) B[i][j] = 0.2 * (A[i][j] + A[i][j - 1] + A[i][1 + j] + A[1 + i][j] + A[i - 1][j]);
    for (let i = 1; i < n - 1; i++)
    for (let j = 1; j < n - 1; j++) A[i][j] = 0.2 * (B[i][j] + B[i][j - 1] + B[i][1 + j] + B[1 + i][j] + B[i - 1][j]);
  }
}
function heat(tsteps: int, n: int, A: number[][][], B: number[][][]): void {
  for (let t = 1; t <= tsteps; t++) {
    for (let i = 1; i < n - 1; i++)
    for (let j = 1; j < n - 1; j++)
    for (let k = 1; k < n - 1; k++)
    B[i][j][k] = 0.125 * (A[i + 1][j][k] - 2.0 * A[i][j][k] + A[i - 1][j][k]) + 0.125 * (A[i][j + 1][k] - 2.0 * A[i][j][k] + A[i][j - 1][k]) +
    0.125 * (A[i][j][k + 1] - 2.0 * A[i][j][k] + A[i][j][k - 1]) + A[i][j][k];
    for (let i = 1; i < n - 1; i++)
    for (let j = 1; j < n - 1; j++)
    for (let k = 1; k < n - 1; k++)
    A[i][j][k] = 0.125 * (B[i + 1][j][k] - 2.0 * B[i][j][k] + B[i - 1][j][k]) + 0.125 * (B[i][j + 1][k] - 2.0 * B[i][j][k] + B[i][j - 1][k]) +
    0.125 * (B[i][j][k + 1] - 2.0 * B[i][j][k] + B[i][j][k - 1]) + B[i][j][k];
  }
}
function fdtd(tmax: int, nx: int, ny: int, ex: number[][], ey: number[][], hz: number[][]): void {
  for (let t = 0; t < tmax; t++) {
    for (let i = 1; i < nx; i++)
    for (let j = 0; j < ny; j++) ey[i][j] = ey[i][j] - 0.5 * (hz[i][j] - hz[i - 1][j]);
    for (let i = 0; i < nx; i++)
    for (let j = 1; j < ny; j++) ex[i][j] = ex[i][j] - 0.5 * (hz[i][j] - hz[i][j - 1]);
    for (let i = 0; i < nx - 1; i++)
    for (let j = 0; j < ny - 1; j++) hz[i][j] = hz[i][j] - 0.7 * (ex[i][j + 1] - ex[i][j] + ey[i + 1][j] - ey[i][j]);
  }
}
// a row of B replaced at each step: no guard kept across steps
function swapping(tsteps: int, n: int, A: number[][], B: number[][], spare: number[]): void {
  for (let t = 0; t < tsteps; t++) {
    B[2] = t % 2 === 1 ? spare : B[2].slice();
    for (let i = 1; i < n - 1; i++)
    for (let j = 1; j < n - 1; j++) B[i][j] = 0.5 * (A[i - 1][j] + A[i + 1][j]);
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
function cube(n: int, s: int): number[][][] {
  const c: number[][][] = [];
  for (let i = 0; i < n; i++) c.push(grid(n, n, s + i));
  return c;
}
function sum(g: number[][]): number {
  let s = 0.0;
  for (const r of g) for (let j = 0; j < r.length; j++) s = s * 0.75 + r[j];
  return s;
}
let A = grid(17, 17, 1), B = grid(17, 17, 2);
jacobi(4, 17, A, B);
console.log("jacobi", sum(A), sum(B));
A = grid(17, 17, 1);
jacobi(3, 17, A, A);
console.log("same", sum(A));
A = grid(17, 17, 1); B = grid(17, 17, 2);
B[5] = B[9];
jacobi(3, 17, A, B);
console.log("twice", sum(A), sum(B));
A = grid(17, 17, 1); B = grid(17, 17, 2);
A.reverse(); B.reverse();
jacobi(3, 17, A, B);
console.log("reversed", sum(A), sum(B));
const C = cube(9, 1), D = cube(9, 2);
heat(3, 9, C, D);
let h = 0.0;
for (const g of C) h = h * 0.5 + sum(g);
console.log("heat", h);
const ex = grid(13, 11, 1), ey = grid(13, 11, 2), hz = grid(13, 11, 3);
fdtd(4, 13, 11, ex, ey, hz);
console.log("fdtd", sum(ex), sum(ey), sum(hz));
A = grid(12, 12, 1); B = grid(12, 12, 2);
swapping(4, 12, A, B, A[6]);
console.log("swapping", sum(A), sum(B));
