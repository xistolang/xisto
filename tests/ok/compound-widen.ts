// XB-094: a let with no type written that is a number takes an int with += -= *= /= (as JavaScript adds it): the
// dev never chose the number, so no conversion is asked for (total += x with x from an int[])
class P {
  n: int;
  constructor(n: int) {
    this.n = n;
  }
}
class Tree {
  l: Tree | null;
  r: Tree | null;
  constructor(l: Tree | null, r: Tree | null) {
    this.l = l;
    this.r = r;
  }
}
function make(d: int): Tree {
  return d === 0 ? new Tree(null, null) : new Tree(make(d - 1), make(d - 1));
}
function check(t: Tree): int {
  return 1 + (t.l === null ? 0 : check(t.l) + check(t.r!));
}
function work(depth: int): string {
  let sum = 0;
  for (let i = 0; i < 4; i++) sum += check(make(depth));
  return `${sum}`;
}
function total(xs: int[]): number {
  let t = 0;
  for (const x of xs) t += x;
  return t / 2;
}
function byIndex(xs: int[]): number {
  let s = 0;
  for (let i = 0; i < xs.length; i++) s += xs[i];
  return s;
}
function fields(ps: P[]): number {
  let s = 0.5;
  let c = 0;
  for (const p of ps) c += p.n;
  for (const p of ps) s -= p.n;
  return s + c;
}
function entries(m: Map<string, int>): number {
  let s = 0;
  for (const [, v] of m) s += v;
  for (const v of m.values()) s *= v;
  return s;
}
function products(xs: int[]): number {
  let s = 0;
  for (const x of xs) s += x * 2 + 1;
  let q = 100;
  for (const x of xs) q /= x;
  return s + q;
}
function zeros(xs: int[]): number {
  let s = -0;
  for (const x of xs) s += x * -1;
  return 1 / s;
}
const m = new Map<string, int>([
  ["a", 2],
  ["b", 3],
]);
console.log(work(3), total([1, 2, 3]), byIndex([4, 5]), fields([new P(3), new P(4)]));
console.log(entries(m), products([1, 2, 4]), zeros([0]), zeros([]));
