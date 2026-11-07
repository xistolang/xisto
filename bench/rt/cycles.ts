// Cycles under load: 2000 rounds of a tree with parent pointers (1 root, 50 children, 5 grandchildren each)
// and a 100-node doubly linked list, all dropped at the end of each round. Refcounting alone would keep
// every one; the cycle collector must free them while the program runs.
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
}
class ListNode {
  value: int;
  prev: ListNode | null = null;
  next: ListNode | null = null;
  constructor(value: int) {
    this.value = value;
  }
}
let total: int = 0;
for (let round: int = 0; round < 2000; round++) {
  const root = new TreeNode("root");
  for (let i: int = 0; i < 50; i++) {
    const c = root.add(new TreeNode("child"));
    for (let j: int = 0; j < 5; j++) {
      c.add(new TreeNode("leaf"));
    }
  }
  let head: ListNode | null = null;
  let tail: ListNode | null = null;
  for (let i: int = 0; i < 100; i++) {
    const n = new ListNode(i);
    if (tail === null) {
      head = n;
    } else {
      tail.next = n;
      n.prev = tail;
    }
    tail = n;
  }
  total += root.children.length + (tail !== null ? tail.value : 0);
}
console.log("total", total);
