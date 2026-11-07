// binary-trees (the Benchmarks Game classic, depth 18): allocation and freeing of many short-lived trees,
// the workload garbage collectors are tuned for.
class Tree {
  left: Tree | null;
  right: Tree | null;
  constructor(left: Tree | null, right: Tree | null) {
    this.left = left;
    this.right = right;
  }
  check(): int {
    const l = this.left;
    const r = this.right;
    return l === null || r === null ? 1 : 1 + l.check() + r.check();
  }
}
function make(depth: int): Tree {
  return depth === 0 ? new Tree(null, null) : new Tree(make(depth - 1), make(depth - 1));
}
const maxDepth: int = 18;
const stretch = make(maxDepth + 1);
console.log("stretch tree of depth", maxDepth + 1, "check:", stretch.check());
const longLived = make(maxDepth);
for (let d: int = 4; d <= maxDepth; d += 2) {
  const iterations: int = 1 << (maxDepth - d + 4);
  let check: int = 0;
  for (let i: int = 0; i < iterations; i++) {
    check += make(d).check();
  }
  console.log(iterations, "trees of depth", d, "check:", check);
}
console.log("long lived tree of depth", maxDepth, "check:", longLived.check());
