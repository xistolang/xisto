// expect: cannot infer the type arguments of Stack
class Stack<T> {
  items: T[] = [];
}
const s = new Stack();
console.log(s.items.length);
