// A dual class (made inside a region loop and outside it) built by a recursive function: inside the loop the
// function's region version allocates from the region directly; outside, the generic one allocates on the heap.
// Both must agree, the objects that leave the loop must survive it, and two regions must not mix.
class Node {
  left: Node | null;
  right: Node | null;
  constructor(left: Node | null, right: Node | null) {
    this.left = left;
    this.right = right;
  }
}
function build(depth: int): Node {
  if (depth === 0) return new Node(null, null);
  return new Node(build(depth - 1), build(depth - 1));
}
function count(n: Node): int {
  const l = n.left;
  const r = n.right;
  return 1 + (l === null ? 0 : count(l)) + (r === null ? 0 : count(r));
}
function wrap(depth: int): Node {
  // not recursive itself; it calls the builder, so it has a region version too
  return new Node(build(depth), null);
}
class Pair {
  a: int;
  b: int;
  constructor(a: int, b: int) {
    this.a = a;
    this.b = b;
  }
}
function pairs(k: int): Pair[] {
  const out: Pair[] = [];
  for (let i = 0; i < k; i++) out.push(new Pair(i, i * i));
  return out;
}

// outside any loop: heap
const kept = build(4);
console.log("kept", count(kept));

// the loop whose iterations' trees die with them (region), the builder called inside and outside
let total: int = 0;
for (let d = 1; d <= 6; d++) {
  const t = build(d);
  const w = wrap(d - 1);
  total += count(t) + count(w);
}
console.log("total", total, "kept still", count(kept));

// an object that leaves the loop is not the region's: it must survive
let escaped: Node | null = null;
let acc: int = 0;
for (let d = 1; d <= 5; d++) {
  const t = build(d);
  acc += count(t);
  if (d === 3) escaped = t;
}
console.log("acc", acc, "escaped", escaped === null ? -1 : count(escaped));

// a second region with another class, and the first builder used again in a loop
let sum: int = 0;
for (let k = 1; k <= 20; k++) {
  const ps = pairs(k);
  for (const p of ps) sum += p.a + p.b;
}
let again: int = 0;
for (let d = 1; d <= 4; d++) again += count(build(d));
console.log("sum", sum, "again", again);

// made before the first iteration (the for's init) or as the iterable: heap objects, kept past the loop
const keptNodes: Node[] = [];
function keepNode(n: Node): int {
  keptNodes.push(n);
  return 0;
}
for (let i: int = keepNode(build(2)); i < 2; i++) {
  const t = build(i);
  keepNode(wrap(0));
  total += count(t);
}
console.log("init kept", count(keptNodes[0]), keptNodes.length, total);
