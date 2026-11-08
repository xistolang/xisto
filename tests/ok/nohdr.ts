// objects without a header (emit.c nohdr_compute): a tree class made in a loop's region and once outside it
// qualifies; the same shapes printed, kept in an array, or with a field written later keep their header. The
// answers are the same either way (and with XISTO_NO_NOHDR=1 or XISTO_REGION_CHECK=1).
class Node {
  left: Node | null;
  right: Node | null;
  constructor(left: Node | null, right: Node | null) {
    this.left = left;
    this.right = right;
  }
}
function make(d: int): Node {
  return d > 0 ? new Node(make(d - 1), make(d - 1)) : new Node(null, null);
}
function count(n: Node): int {
  const l = n.left;
  if (l === null) return 1;
  return 1 + count(l) + count(n.right!);
}

class Shown {
  a: Shown | null;
  v: int;
  constructor(a: Shown | null, v: int) {
    this.a = a;
    this.v = v;
  }
}
function chain(k: int): Shown {
  return k > 0 ? new Shown(chain(k - 1), k) : new Shown(null, 0);
}

class Kept {
  next: Kept | null;
  constructor(next: Kept | null) {
    this.next = next;
  }
}
function list(k: int): Kept {
  return k > 0 ? new Kept(list(k - 1)) : new Kept(null);
}
function len(k: Kept): int {
  const n = k.next;
  return n === null ? 1 : 1 + len(n);
}

class Moved {
  to: Moved | null;
  constructor(to: Moved | null) {
    this.to = to;
  }
}

function run(): void {
  const long = make(6);
  let total: int = 0;
  for (let i = 0; i < 50; i++) total += count(make(i % 7));
  console.log(total, count(long));

  let shown: int = 0;
  for (let i = 0; i < 5; i++) shown += chain(i).v;
  console.log(shown, chain(2));

  const keep: Kept[] = [];
  for (let i = 0; i < 4; i++) keep.push(list(i));
  let sum: int = 0;
  for (const k of keep) sum += len(k);
  console.log(sum);

  let hops: int = 0;
  for (let i = 0; i < 10; i++) {
    const a = new Moved(null);
    const b = new Moved(a);
    b.to = null;
    if (b.to === null) hops++;
  }
  console.log(hops);
}
run();
