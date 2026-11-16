// XB-210: `for (const x of a)` with a declared int[]: x is an int by its form, as a[i] is, so `let t = 0; t += x`
// keeps t an int (an index below needs one)
// warns: none
function pick(xs: int[], names: string[]): string {
  let t = 0;
  for (const x of xs) t += x;
  return names[t % names.length];
}
function spread(xs: int[]): number {
  let lo = 0, hi = 0;
  for (const x of xs) { if (x < lo) lo = x; if (x > hi) hi = x; }
  return (hi - lo) / 2;
}
console.log(pick([1, 2, 3], ["a", "b", "c", "d"]), spread([4, -3, 9]), spread([]));
