// XB-088: a variable every assignment of which is an integer literal or `% K` (K a literal) is an int, even through a
// product, when the bound of the expression stays in the safe range: `last = (last * A + C) % M`, as in an LCG.
let last = 42;
const A = 3877, C = 29573, M = 139968;
function rand(max: number): number {
  last = (last * A + C) % M;
  return max * last / M;
}
function other(): string {
  let last = ""; // its own: does not count against the global
  for (const c of ["x", "y"]) last = last + c;
  return last;
}
let sum = 0;
for (let i = 0; i < 100000; i++) sum += rand(1);
console.log(sum.toFixed(6), last, other());
// a local one, negative steps included (the remainder keeps the sign)
function walk(n: number): number {
  let h = 7;
  for (let i = 0; i < n; i++) h = (h * 31 - 1000) % 1009;
  return h;
}
console.log(walk(1000), walk(3), walk(0));
// too large a product for the safe range: stays a number (rounds as JavaScript does)
let big = 3;
for (let i = 0; i < 20; i++) big = (big * 67108859 + 12345) % 67108864;
console.log(big);
// also written another way: not by its form an int, a number as before
let mixed = 5;
mixed = (mixed * 3) % 7;
mixed = mixed / 2;
console.log(mixed);
