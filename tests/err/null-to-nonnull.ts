// expect: it may be null; check first
class Node {
  value: int = 0;
}
let n: Node | null = null;
const m: Node = n;
