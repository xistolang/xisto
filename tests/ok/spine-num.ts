// The number spine: `if (base) return K; return c + f(a) + f(b)` with number results and whole-number literals
// becomes a loop with an int64 sum; once a partial sum would pass 2^53 - 1 it computes the nested double form as
// written, so the result is Node's bit for bit, rounding included.
class Node2 {
  left: Node2 | null;
  right: Node2 | null;
  constructor(left: Node2 | null, right: Node2 | null) {
    this.left = left;
    this.right = right;
  }
}

function build(depth: int): Node2 {
  return depth > 0 ? new Node2(build(depth - 1), build(depth - 1)) : new Node2(null, null);
}

// a DAG: both children the same node, so a depth-d spine visits 2^d leaves with few objects
function shared(depth: int): Node2 {
  let n = new Node2(null, null);
  for (let i = 0; i < depth; i++) n = new Node2(n, n);
  return n;
}

function count(node: Node2): number {
  if (node.left === null) return 1;
  return 1 + count(node.left) + count(node.right!);
}

// a big base value: 2^24 leaves of 1e9 pass 2^53, where the doubles round; the result must round as Node's
function heavy(node: Node2): number {
  if (node.left === null) return 1000000000;
  return 7 + heavy(node.left) + heavy(node.right!);
}

// not the spine: a fractional term, a subtraction
function half(node: Node2): number {
  if (node.left === null) return 1;
  return 0.5 + half(node.left) + half(node.right!);
}
function diff(node: Node2): number {
  if (node.left === null) return 1;
  return 1 + diff(node.left) - diff(node.right!);
}

const t = build(10);
console.log(count(t), half(t), diff(t));
console.log(count(shared(20)), heavy(shared(10)));
console.log(heavy(shared(24)), heavy(shared(25)));
