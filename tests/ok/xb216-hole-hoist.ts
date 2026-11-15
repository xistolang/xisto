// XB-216 (hoisted test): a loop that reads through the same row in every iteration (X[r][j], X[r].f, X[r].length,
// r invariant) tests that row once before the loop; a hole still throws Node's TypeError where Node throws it (after
// what the loop did before reaching the read), a loop that runs zero times throws nothing, and a row the loop changes
// is still tested at each read
class P { x = 1; }
function report(e: unknown): void { console.log((e as Error).name + ": " + (e as Error).message); }
function rows(n: int, filled: int[]): number[][] {
  const X: number[][] = new Array<number[]>(n);
  for (const r of filled) X[r] = [r, r + 1, r + 2];
  return X;
}
function sumRow(X: number[][], r: int, n: int): number {
  let s = 0;
  for (let j = 0; j < n; j++) {
    console.log("step", j);
    s += X[r][j];
  }
  return s;
}
function scale(X: number[][], r: int, n: int, f: number): void {
  for (let j = 0; j < n; j++) X[r][j] *= f;
}
function gemmLike(C: number[][], A: number[][], B: number[][], n: int): void {
  for (let i = 0; i < n; i++)
  for (let k = 0; k < n; k++)
  for (let j = 0; j < n; j++) C[i][j] += A[i][k] * B[k][j];
}
const X = rows(3, [0, 2]);
console.log(sumRow(X, 0, 3), sumRow(X, 2, 2));
try { console.log(sumRow(X, 1, 3)); } catch (e) { report(e); }  // "step 0" first, then the TypeError
console.log(sumRow(X, 1, 0));                                   // zero iterations: no error
scale(X, 2, 3, 2);
console.log(X[2].join(","));
try { scale(X, 1, 2, 2); } catch (e) { report(e); }             // as `=` target: "Cannot read" (a compound)
scale(X, 1, 0, 2);
// a store
function fill(X: number[][], r: int, n: int): void { for (let j = 0; j < n; j++) X[r][j] = j; }
try { fill(X, 1, 2); } catch (e) { report(e); }
fill(X, 1, 0);
// the length in the condition
function lenLoop(X: number[][], r: int): number { let s = 0; for (let j = 0; j < X[r].length; j++) s += X[r][j]; return s; }
console.log(lenLoop(X, 0));
try { console.log(lenLoop(X, 1)); } catch (e) { report(e); }
// a while loop
function whileLoop(X: number[][], r: int, n: int): number { let s = 0; let j = 0; while (j < n) { s += X[r][j]; j++; } return s; }
console.log(whileLoop(X, 0, 3), whileLoop(X, 1, 0));
try { console.log(whileLoop(X, 1, 1)); } catch (e) { report(e); }
// rows of objects
const ps: P[] = new Array<P>(3);
ps[0] = new P(); ps[2] = new P();
function objLoop(ps: P[], r: int, n: int): number { let s = 0; for (let j = 0; j < n; j++) { s += ps[r].x; ps[r].x = j; } return s; }
console.log(objLoop(ps, 0, 3), objLoop(ps, 1, 0));
try { console.log(objLoop(ps, 1, 2)); } catch (e) { report(e); }
// three dimensions: A[r][q][s]
const A: number[][][] = new Array<number[][]>(2);
A[0] = new Array<number[]>(2);
A[0][0] = [1, 2, 3];
function sum3(A: number[][][], r: int, q: int, n: int): number { let s = 0; for (let k = 0; k < n; k++) s += A[r][q][k]; return s; }
console.log(sum3(A, 0, 0, 3), sum3(A, 1, 0, 0), sum3(A, 0, 1, 0));
try { console.log(sum3(A, 0, 1, 1)); } catch (e) { report(e); }
try { console.log(sum3(A, 1, 1, 1)); } catch (e) { report(e); }
// a row the loop changes: the row index moves, a hole is stored in its slot, the array is another one, a call
// changes it
function moving(X: number[][], n: int): number { let s = 0; let r = 0; for (let j = 0; j < n; j++) { s += X[r][0]; r = j + 1; } return s; }
try { console.log(moving(X, 3)); } catch (e) { report(e); }
const E = rows(3, [0]);
function emptied(X: number[][], n: int): number { let s = 0; for (let j = 0; j < n; j++) { s += X[2][0]; if (j === 1) X[2] = E[2]; } return s; }
try { console.log(emptied(rows(3, [0, 1, 2]), 4)); } catch (e) { report(e); }
function swap(n: int): number {
  let Y = rows(2, [1]);
  const Z = rows(2, [0]);
  let s = 0;
  for (let j = 0; j < n; j++) { s += Y[1][j]; Y = Z; }
  return s;
}
try { console.log(swap(3)); } catch (e) { report(e); }
const G = rows(3, [0, 1, 2]);
function cut(): void { G[1] = E[1]; }
function viaCall(n: int): number { let s = 0; for (let j = 0; j < n; j++) { s += G[1][j]; cut(); } return s; }
try { console.log(viaCall(2)); } catch (e) { report(e); }
// filled matrices: the loop as fast as before, the same numbers
const n: int = 4;
const C = rows(n, [0, 1, 2, 3]), Am = rows(n, [0, 1, 2, 3]), Bm = rows(n, [0, 1, 2, 3]);
for (let r = 0; r < 4; r++) { C[r].push(0); Am[r].push(1); Bm[r].push(2); }
gemmLike(C, Am, Bm, n);
console.log(C.map((r) => r.join(",")).join(" | "));
const H = rows(n, [0, 1, 3]);
for (const r of [0, 1, 3] as int[]) H[r].push(5);
try { gemmLike(H, Am, Bm, n); } catch (e) { report(e); }
console.log(([0, 1, 3] as int[]).map((r) => H[r].join(",")).join(" | "));
try { gemmLike(C, H, Bm, n); } catch (e) { report(e); }
