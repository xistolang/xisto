// ++i and i += 1 in a for's update are i++ (emit.c, mark_counted_inc): the increment unchecked, the loop
// versioned like any counted loop; a body that writes i, or another step, keeps the checked form
function aat(n: int, A: number[][]): number[][] {
  const B: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  for (let t = 0; t < n; ++t)
  for (let r = 0; r < n; r += 1)
  for (let s = 0; s < n; ++s)
  B[r][s] += A[r][t] * A[s][t];
  return B;
}
const A: number[][] = [];
for (let i = 0; i < 4; ++i) {
  const row: number[] = [];
  for (let j = 0; j < 4; j += 1) row.push(((i + 2 * j) % 5) / 5);
  A.push(row);
}
console.log(aat(4, A).map((r) => r.map((x) => x.toFixed(3)).join(",")).join(" "));
let seen = "";
for (let i = 0; i < 10; ++i) {
  if (i % 3 === 0) i += 1;
  seen += String(i);
}
console.log("written in the body", seen);
let k = 0;
for (let i = 0; i < 10; i += 2) k += i;
for (let i = 0; i <= 3; ++i) k += i;
console.log("other steps", k);
