// Regions (region.c): objects that cannot outlive a loop iteration are freed with it. Every escape below must
// keep its objects alive (no region for that class), and the confined ones must behave like any object.

// confined: a tree with parent pointers and a doubly linked list per iteration (region)
class Node {
  name: string;
  parent: Node | null = null;
  kids: Node[] = [];
  constructor(name: string) {
    this.name = name;
  }
  add(c: Node): Node {
    c.parent = this;
    this.kids.push(c);
    return c;
  }
  depth(): int {
    let d: int = 0;
    let p = this.parent;
    while (p !== null) {
      d++;
      p = p.parent;
    }
    return d;
  }
}
let sum: int = 0;
for (let r: int = 0; r < 300; r++) {
  const root = new Node(`r${r}`);
  for (let i: int = 0; i < 4; i++) {
    const c = root.add(new Node(`c${i}`));
    c.add(new Node("leaf")).add(new Node("deep"));
  }
  sum += root.kids.length + root.kids[3].kids[0].kids[0].depth();
  if (r === 299) {
    console.log(root.kids[2].name, root.kids[2].parent === root, root.kids[1].kids[0].name);
  }
}
console.log("tree", sum);

// escapes to a variable declared before the loop: no region
class Kept {
  v: int;
  next: Kept | null = null;
  constructor(v: int) {
    this.v = v;
  }
}
let last: Kept | null = null;
for (let i: int = 0; i < 5; i++) {
  const k = new Kept(i);
  k.next = last;
  last = k;
}
let n: int = 0;
let p = last;
while (p !== null) {
  n += p.v;
  p = p.next;
}
console.log("kept", n);

// escapes into an array made outside the loop: no region
class Item {
  label: string;
  constructor(label: string) {
    this.label = label;
  }
}
const items: Item[] = [];
for (let i: int = 0; i < 3; i++) {
  items.push(new Item(`item${i}`));
}
console.log(items.map((it: Item): string => it.label).join(","));

// escapes through a function that stores into a module-level map: no region
class Entry {
  key: string;
  constructor(key: string) {
    this.key = key;
  }
}
const registry = new Map<string, Entry>();
function remember(e: Entry): void {
  registry.set(e.key, e);
}
for (let i: int = 0; i < 3; i++) {
  remember(new Entry(`e${i}`));
}
const e1 = registry.get("e1");
console.log("registry", registry.size, e1 !== null ? e1.key : "none");

// escapes through a holder class made outside the loop: no region
class Leaf {
  w: int;
  constructor(w: int) {
    this.w = w;
  }
}
class Holder {
  leaves: Leaf[] = [];
}
const holder = new Holder();
for (let i: int = 0; i < 4; i++) {
  holder.leaves.push(new Leaf(i * 10));
}
console.log("holder", holder.leaves.length, holder.leaves[3].w);

// escapes by return from the function that runs the loop: no region
class Res {
  total: int = 0;
}
function build(): Res {
  let best = new Res();
  for (let i: int = 0; i < 3; i++) {
    const r = new Res();
    r.total = i * 7;
    if (r.total > best.total) {
      best = r;
    }
  }
  return best;
}
console.log("best", build().total);

// a cycle that does escape: kept by the cycle collector, not by a region
class Peer {
  other: Peer | null = null;
  id: int;
  constructor(id: int) {
    this.id = id;
  }
}
const pairs: Peer[] = [];
for (let i: int = 0; i < 3; i++) {
  const a = new Peer(i);
  const b = new Peer(i + 100);
  a.other = b;
  b.other = a;
  pairs.push(a);
}
const o = pairs[2].other;
const oo = o !== null ? o.other : null;
console.log("pairs", o !== null ? o.id : -1, oo !== null ? oo.id : -1);

// confined, with strings made per iteration held by the objects (dropped with the region)
class Tag {
  text: string;
  prev: Tag | null = null;
  constructor(text: string) {
    this.text = text;
  }
}
let chars: int = 0;
for (let i: int = 0; i < 50; i++) {
  let t: Tag | null = null;
  for (let j: int = 0; j < 10; j++) {
    const nt = new Tag(`tag-${i}-${j}`);
    nt.prev = t;
    t = nt;
  }
  while (t !== null) {
    chars += t.text.length;
    t = t.prev;
  }
}
console.log("tags", chars);
