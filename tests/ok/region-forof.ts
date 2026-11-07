// Regions over for...of loops on arrays (XB-028): each element's trees (with parent pointers, cycles) are freed
// in one step per iteration; a loop whose objects escape (pushed outside, returned) keeps counting references
class TreeNode {
  left: TreeNode | null = null;
  right: TreeNode | null = null;
  parent: TreeNode | null = null;
  val: int;
  constructor(v: int) { this.val = v; }
}
function build(depth: int, v: int): TreeNode {
  const n = new TreeNode(v);
  if (depth > 0) {
    const l = build(depth - 1, v * 2);
    const r = build(depth - 1, v * 2 + 1);
    l.parent = n; r.parent = n;
    n.left = l; n.right = r;
  }
  return n;
}
function sum(n: TreeNode | null): int {
  if (n === null) { return 0; }
  return n.val + sum(n.left) + sum(n.right);
}
const depths: int[] = [];
for (let i: int = 0; i < 40; i++) { depths.push(12 + (i % 3)); }
let total: int = 0;
for (const d of depths) {
  const t = build(d, 1);
  total += sum(t);
}
console.log(total);
class Item { name: string; constructor(n: string) { this.name = n; } }
const names: string[] = ["a", "b", "c"];
const kept: Item[] = [];
for (const n of names) {
  const it = new Item(n.toUpperCase());
  kept.push(it); // escapes: no region for Item
}
function firstLong(xs: string[]): Item | null {
  for (const x of xs) {
    const it = new Item(x);
    if (x.length > 1) { return it; } // returned: no region
  }
  return null;
}
const f = firstLong(["x", "yy", "zzz"]);
console.log(kept.map((i: Item) => i.name).join(""), f !== null ? f.name : "none");
