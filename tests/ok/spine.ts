// Recursion on the right spine: a function returning c + f(x.a) + f(x.b) over int, with a constant base case,
// is emitted as a loop on the second child (the first stays recursive). Same sums, same order of visits, same
// overflow behaviour. Shapes that do not qualify (subtraction, number results, a third call) stay as written.
class Node {
  left: Node | null;
  right: Node | null;
  weight: int;
  constructor(left: Node | null, right: Node | null, weight: int) {
    this.left = left;
    this.right = right;
    this.weight = weight;
  }
}
function full(depth: int, w: int): Node {
  if (depth === 0) return new Node(null, null, w);
  return new Node(full(depth - 1, w + 1), full(depth - 1, w + 2), w);
}
// a long right spine with small left bushes: the loop runs many times per call
function comb(n: int): Node {
  let t: Node = new Node(null, null, 0);
  for (let i = 0; i < n; i++) t = new Node(full(i % 4, i), t, i);
  return t;
}
// a long left spine: the recursion goes deep on the left, the loop runs once per level
function lcomb(n: int): Node {
  let t: Node = new Node(null, null, 0);
  for (let i = 0; i < n; i++) t = new Node(t, full(i % 3, i), i);
  return t;
}

// qualifies: base 1, step 1 + f(l) + f(r!)
function checksum(node: Node): int {
  const l = node.left;
  if (l === null) return 1;
  return 1 + checksum(l) + checksum(node.right!);
}
// qualifies: base 3, constants 2 + 5 split around the calls, test on the field itself
function weird(node: Node): int {
  if (node.left === null) return 3;
  return 2 + weird(node.left!) + 5 + weird(node.right!);
}
// qualifies: base 0 and no constant: just the count of internal nodes... (every leaf adds 0)
function inner(node: Node): int {
  const l = node.left;
  const r = node.right;
  if (l === null) return 0;
  return inner(l) + inner(r!) + 1;
}
// does not qualify: a subtraction (the sums are not monotone)
function diff(node: Node): int {
  const l = node.left;
  if (l === null) return 1;
  return 1 + diff(l) - diff(node.right!);
}
// does not qualify: number (reassociation would change rounding in general)
function fsum(node: Node): number {
  const l = node.left;
  if (l === null) return 0.1;
  return 0.1 + fsum(l) + fsum(node.right!);
}
// does not qualify: the weight is read (not a constant), three terms with a field
function weighted(node: Node): int {
  const l = node.left;
  if (l === null) return node.weight;
  return node.weight + weighted(l) + weighted(node.right!);
}

const t = full(6, 0);
console.log(checksum(t), weird(t), inner(t), diff(t), fsum(t).toFixed(6), weighted(t));
const c = comb(2000);
console.log(checksum(c), weird(c), inner(c), diff(c), weighted(c));
const lc = lcomb(300);
console.log(checksum(lc), weird(lc), inner(lc), diff(lc), weighted(lc));
// a one-node tree and a two-level one
console.log(checksum(new Node(null, null, 0)), weird(new Node(new Node(null, null, 1), new Node(null, null, 2), 0)));
