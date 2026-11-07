// expect: 'value' may be null
class Node {
  value: int = 0;
}
let n: Node | null = null;
console.log(n.value);
