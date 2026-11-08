// expect: runtime error: integer overflow: the result left JavaScript's safe integer range
// the spine loop (a recursive sum over a tree) tests the safe range once, on the final value: three leaves worth
// 2^52 each pass 2^53, and the error comes before anything reads the sum
class N {
  left: N | null;
  right: N | null;
  constructor(l: N | null, r: N | null) { this.left = l; this.right = r; }
}
function weigh(n: N): int {
  const l = n.left;
  if (l === null) return 4503599627370496;
  return weigh(l) + weigh(n.right!);
}
const leaf = () => new N(null, null);
console.log(weigh(new N(new N(leaf(), leaf()), leaf())));
