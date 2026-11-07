// Reference cycles are collected (and the live ones kept): tree with parent pointers, doubly linked list,
// self reference, a closure stored in the object it captures, a Map that points back to its owner, cycles
// through subclasses and unions, and a cycle that stays reachable from a global while its siblings go.
class TreeNode {
  name: string;
  parent: TreeNode | null = null;
  children: TreeNode[] = [];
  constructor(name: string) {
    this.name = name;
  }
  add(c: TreeNode): TreeNode {
    c.parent = this;
    this.children.push(c);
    return c;
  }
  path(): string {
    const p = this.parent;
    return p === null ? this.name : p.path() + "/" + this.name;
  }
}
class ListNode {
  value: int;
  prev: ListNode | null = null;
  next: ListNode | null = null;
  constructor(value: int) {
    this.value = value;
  }
}
class Widget {
  label: string;
  onClick: () => string;
  self: Widget | null = null;
  constructor(label: string) {
    this.label = label;
    this.onClick = (): string => "clicked " + this.label;
  }
}
class Registry {
  items: Map<string, Entry> = new Map<string, Entry>();
}
class Entry {
  owner: Registry;
  id: string;
  constructor(owner: Registry, id: string) {
    this.owner = owner;
    this.id = id;
  }
}
class Animal {
  friend: Animal | null = null;
  name(): string {
    return "animal";
  }
}
class Dog extends Animal {
  name(): string {
    return "dog";
  }
}

// cycles made inside constructors: `this` handed to a child, to another object, kept in itself
class Owner {
  child: Owned;
  constructor() {
    this.child = new Owned(this);
  }
}
class Owned {
  owner: Owner;
  constructor(owner: Owner) {
    this.owner = owner;
  }
}
class Peer {
  other: Peer | null = null;
  constructor(other: Peer | null) {
    if (other !== null) {
      this.other = other;
      other.other = this;
    }
  }
}
interface Link {
  next: Link | null;
  tag: string;
}

const kept = new TreeNode("kept");
let paths = "";
for (let round: int = 0; round < 300; round++) {
  const root = new TreeNode("root" + String(round));
  const a = root.add(new TreeNode("a"));
  const b = a.add(new TreeNode("b"));
  if (round === 299) {
    paths = b.path();
  }
  if (round % 100 === 0) {
    kept.add(root); // this tree stays alive, reachable from kept
  }

  let head: ListNode | null = null;
  let tail: ListNode | null = null;
  for (let i: int = 0; i < 20; i++) {
    const n = new ListNode(i);
    if (tail === null) {
      head = n;
    } else {
      tail.next = n;
      n.prev = tail;
    }
    tail = n;
  }

  const w = new Widget("w" + String(round));
  w.self = w;

  const reg = new Registry();
  for (let i: int = 0; i < 5; i++) {
    reg.items.set("e" + String(i), new Entry(reg, "e" + String(i)));
  }

  const o = new Owner();
  const p1 = new Peer(null);
  const p2 = new Peer(p1);
  const l1: Link = { next: null, tag: "a" };
  const l2: Link = { next: l1, tag: "b" };
  l1.next = l2;
  if (round === 0) {
    console.log(o.child.owner === o, p1.other === p2, l1.next === l2, l2.next === l1, o.child.owner.child === o.child);
  }

  const d1 = new Dog();
  const d2 = new Dog();
  d1.friend = d2;
  d2.friend = d1;
}
console.log(paths, kept.children.length, kept.children[2].children[0].path());
console.log(kept.children[1].children[0].children[0].name, kept.children[0].name);
const w = new Widget("last");
console.log(w.onClick());
