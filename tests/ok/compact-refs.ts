// header-less classes whose references are 32 bits (emit.c cptr_compute) and statements whose objects die with
// them (emit.c once_compute): the same answers as Node's, with the trees made in a loop's region, outside it (kept
// for the whole run), and in a statement that gives them back; a tree stored in a global keeps its objects.
class Node {
  left: Node | null;
  right: Node | null;
  value: int;
  constructor(left: Node | null, right: Node | null, value: int) {
    this.left = left;
    this.right = right;
    this.value = value;
  }
}
function make(d: int): Node {
  return d > 0 ? new Node(make(d - 1), make(d - 1), d) : new Node(null, null, 1);
}
function count(n: Node): int {
  const l = n.left;
  if (l === null) return n.value;
  return n.value + count(l) + count(n.right!);
}
function depth(n: Node | null): int {
  return n === null ? 0 : 1 + Math.max(depth(n.left), depth(n.right));
}

let kept: Node | null = null;
function keep(d: int): int { // its tree outlives the statement that makes it: in a global
  kept = make(d);
  return count(kept);
}

function main(): void {
  const stretch = count(make(14)); // made and dropped in the statement: given back after it
  console.log("stretch", stretch);
  const long = make(10); // kept: its objects stay
  const k = keep(6);
  let total: int = 0;
  for (let i = 0; i < 40; i++) total += count(make(i % 9));
  console.log("loop", total);
  if (total > 0) {
    const again = count(make(12)) + depth(make(5)); // in a branch, still outside loops
    console.log("again", again);
  }
  console.log("long", count(long), depth(long), "kept", k, count(kept!), depth(kept));
}
main();
