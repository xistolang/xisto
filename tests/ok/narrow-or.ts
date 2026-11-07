// Null narrowing through ||, && and ! (as TypeScript): x === null || y === null ? a : (both non-null here),
// early exits, while conditions and the right side of ||.
class Pair {
  left: Pair | null;
  right: Pair | null;
  constructor(left: Pair | null, right: Pair | null) {
    this.left = left;
    this.right = right;
  }
  depth(): int {
    const l = this.left;
    const r = this.right;
    return l === null || r === null ? 1 : 1 + Math.max(l.depth(), r.depth());
  }
}
function describe(a: string | null, b: string | null): string {
  if (a === null || b === null) {
    return "missing";
  }
  return a.toUpperCase() + b.toUpperCase();
}
function firstLen(a: string | null, b: string | null): int {
  if (!(a !== null && b !== null)) {
    return -1;
  }
  return a.length + b.length;
}
function orRight(s: string | null): boolean {
  return s === null || s.length > 3;
}
const t = new Pair(new Pair(null, null), new Pair(new Pair(null, null), new Pair(null, null)));
console.log(t.depth(), describe("a", null), describe("x", "y"), firstLen("ab", "c"), firstLen(null, "c"), orRight(null), orRight("hello"), orRight("hi"));
