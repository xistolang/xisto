// expect: runtime error: index out of bounds
// an index E - i - C (emit.c, bce_cands_e): k past the end of r, so the guard fails and the loop as written
// reports the index, uncaught
function rev(k: int, r: number[], y: number[]): number {
  let s = 0.0;
  for (let i = 0; i < k; i++) s += r[k - i - 1] * y[i];
  return s;
}
const r: number[] = [1, 2, 3, 4];
const y: number[] = [1, 1, 1, 1, 1, 1];
console.log(rev(6, r, y));
