// `const x = param.field` / `const x = this.field` of a field written only during construction: the local
// borrows the reference instead of retaining it. Uses that keep the value (return, push, store, capture)
// still take their own reference, so the value outlives its owner where the program says so.
class Node {
  left: Node | null;
  right: Node | null;
  name: string;
  tag: string[];
  hits: number;
  constructor(name: string, left: Node | null, right: Node | null) {
    this.name = name;
    this.left = left;
    this.right = right;
    this.tag = [name];
    this.hits = 0;
  }
  size(): number {
    const l = this.left;
    const r = this.right;
    return 1 + (l === null ? 0 : l.size()) + (r === null ? 0 : r.size());
  }
}

function build(depth: number, prefix: string): Node {
  if (depth === 0) return new Node(prefix, null, null);
  return new Node(prefix, build(depth - 1, prefix + "L"), build(depth - 1, prefix + "R"));
}

function checksum(node: Node): number {
  const l = node.left;
  if (l === null) return 1;
  const r = node.right!;
  return 1 + checksum(l) + checksum(r);
}

// the borrowed local leaves the function: the caller gets its own reference
function pickLeft(n: Node): Node | null {
  const l = n.left;
  return l;
}

// kept in an array and in another object's field, past the owner's life
class Keeper {
  kept: Node | null;
  constructor() { this.kept = null; }
  keep(n: Node): void {
    const r = n.right;
    this.kept = r;
  }
}

function names(n: Node, into: string[]): void {
  const s = n.name;
  const t = n.tag;
  into.push(s + "/" + t[0]);
  const l = n.left;
  const r = n.right;
  if (l !== null) names(l, into);
  if (r !== null) names(r, into);
}

function later(n: Node): () => string {
  const s = n.name;
  const l = n.left;
  return () => s + (l === null ? "" : ":" + l.name);
}

// a field that is written after construction is not borrowed: the same answers, the usual references
class Cursor {
  cur: Node;
  constructor(n: Node) { this.cur = n; }
  descend(): void {
    const c = this.cur;
    const l = c.left;
    if (l !== null) this.cur = l;
  }
}

function touch(n: Node): number {
  const l = n.left!;
  l.hits = l.hits + 1;
  return l.hits;
}

const root = build(3, "n");
console.log(checksum(root), root.size());

let picked = pickLeft(build(2, "p"));
console.log(picked === null ? "null" : picked.name, picked === null ? 0 : picked.size());

const k = new Keeper();
k.keep(build(1, "k"));
const kept = k.kept;
console.log(kept === null ? "null" : kept.name + " " + String(kept.tag.length));

const all: string[] = [];
names(root, all);
console.log(all.length, all[0], all[all.length - 1]);

const f = later(build(1, "c"));
console.log(f());

const cur = new Cursor(root);
cur.descend();
cur.descend();
console.log(cur.cur.name);

console.log(touch(root), touch(root), root.left!.hits);
