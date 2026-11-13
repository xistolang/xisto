// `for (let i = A; i <= E - c; i++)` (c >= 1) or `i <= L` is a counted loop (emit.c, mark_counted_inc): the increment
// unchecked and the loop versioned; i + 1 is still a safe integer at the last i
function stencil(steps: int, n: int, A: number[][]): void {
  for (let t = 0; t <= steps - 1; t++)
  for (let i = 1; i <= n - 2; i++)
  for (let j = 1; j <= n - 2; j++)
  A[i][j] = (A[i - 1][j - 1] + A[i - 1][j] + A[i - 1][j + 1] + A[i][j - 1] + A[i][j] + A[i][j + 1] + A[i + 1][j - 1] + A[i + 1][j] +
    A[i + 1][j + 1]) / 9.0;
}
const A: number[][] = [];
for (let i = 0; i < 8; i++) {
  const r: number[] = [];
  for (let j = 0; j < 8; j++) r.push(((i * 3 + j) % 5) / 2);
  A.push(r);
}
stencil(3, 8, A);
console.log(A.map((r) => r.map((x) => x.toFixed(6)).join(",")).join(" "));
// at the top of the safe integers: the last i is 2^53 - 2, i + 1 is 2^53 - 1
const big: int = 9007199254740991;
let count = 0;
for (let i = big - 4; i <= big - 1; i++) count++;
console.log("count", count);
let lit = 0;
for (let i = 0; i <= 5; i++) lit += i;
console.log("literal", lit);
// an empty loop (the bound below the start)
let none = 0;
for (let i = 5; i <= 3 - 1; i++) none++;
console.log("none", none);
