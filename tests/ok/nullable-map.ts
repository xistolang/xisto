// T | null with narrowing; Map and Set.
class Node {
  value: int;
  next: Node | null = null;
  constructor(value: int) {
    this.value = value;
  }
}

class LinkedList {
  head: Node | null = null;
  size: int = 0;
  push(value: int): void {
    const n: Node = new Node(value);
    n.next = this.head;
    this.head = n;
    this.size++;
  }
  find(value: int): Node | null {
    let cur: Node | null = this.head;
    while (cur !== null) {
      if (cur.value === value) {
        return cur;
      }
      cur = cur.next;
    }
    return null;
  }
  sum(): int {
    let total: int = 0;
    let cur: Node | null = this.head;
    while (cur !== null) {
      total += cur.value;
      cur = cur.next;
    }
    return total;
  }
}

function describe(n: Node | null): string {
  if (n === null) {
    return "nothing";
  }
  return `node ${n.value}`;
}

const list: LinkedList = new LinkedList();
list.push(1);
list.push(2);
list.push(3);
const found: Node | null = list.find(2);
const missing: Node | null = list.find(9);
console.log(list.size, list.sum(), describe(found), describe(missing), found === null, missing === null);
let name: string | null = null;
console.log(name, name === null);
name = "isa";
if (name !== null && name.length > 0) {
  console.log(name.toUpperCase());
}
const first: Node | null = list.head;
if (first !== null) {
  console.log(first.value, first.next === null);
}

const ages: Map<string, int> = new Map<string, int>();
ages.set("isa", 30).set("ju", 31);
ages.set("isa", 32);
console.log(ages.size, ages.get("isa"), ages.has("ju"), ages.has("x"), ages.delete("ju"), ages.delete("ju"), ages.size);
const byId: Map<int, Node> = new Map<int, Node>();
byId.set(1, new Node(10));
byId.set(2, new Node(20));
const n2: Node | null = byId.get(2);
console.log(n2 === null ? -1 : n2.value, byId.has(3), byId.size);
const seen: Set<string> = new Set<string>();
seen.add("a").add("b").add("a");
console.log(seen.size, seen.has("a"), seen.has("z"));
const ids: Set<int> = new Set<int>();
for (let i: int = 0; i < 100; i++) {
  ids.add(i % 7);
}
console.log(ids.size);
ages.clear();
console.log(ages.size, ages.has("isa"));
console.log(ages, seen);
