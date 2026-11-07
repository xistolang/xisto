// dual regions (region.c): a class made both inside a loop (into the loop's region, freed per iteration) and
// outside it (on the heap): the same functions build both, a method reads both, a heap tree lives across the
// loop, an iteration breaks or continues, and a loop made of other loops keeps its own heap objects
class Node2 {
  left: Node2 | null;
  right: Node2 | null;
  val: int;
  constructor(left: Node2 | null, right: Node2 | null, val: int) {
    this.left = left;
    this.right = right;
    this.val = val;
  }
  sum(): int {
    const l = this.left;
    const r = this.right;
    return this.val + (l === null ? 0 : l.sum()) + (r === null ? 0 : r.sum());
  }
}
function build(d: int, v: int): Node2 {
  return d === 0 ? new Node2(null, null, v) : new Node2(build(d - 1, v * 2), build(d - 1, v * 2 + 1), v);
}
function depthOf(n: Node2): int {
  const l = n.left;
  return l === null ? 0 : 1 + depthOf(l);
}
const kept = build(6, 1); // on the heap: built before the loop, read after it
let total: int = 0;
for (let i: int = 0; i < 50; i++) {
  if (i === 40) break;
  const t = build(i % 7, i);
  if (t.val % 5 === 3) continue;
  total += t.sum() + depthOf(t);
  const pair = new Node2(t, build(2, i), -i); // a region object holding region objects
  total += pair.sum() % 1000;
}
console.log(total, kept.sum(), depthOf(kept));
function work(n: int): int {
  let s: int = 0;
  for (let k: int = 0; k < n; k++) s += build(3, k).sum();
  return s;
}
let outer: int = 0;
for (let j: int = 1; j <= 5; j++) outer += work(j * 3) + build(1, j).val; // not a region: it runs another loop
console.log(outer, build(4, 7).sum());
