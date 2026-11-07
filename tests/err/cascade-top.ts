// expect: xisto: 3 errors
// XB-011: a failed declaration is still declared (poisoned), so its uses add no "unknown variable" errors
function total(xs: number[]): number {
  let s = 0;
  for (const x of xs) {
    s += x;
  }
  return s;
}
const r = totl([1, 2, 3]);
console.log(r + 1);
const name: string = 42;
console.log(name.toUpperCase());
class User {
  constructor(public email: string) {}
}
const u = new User("a@b");
console.log(u.emial);
