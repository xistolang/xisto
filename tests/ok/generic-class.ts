// generic classes: one class per list of type arguments (Stack<number>, Stack<string>), two parameters,
// a field of type T | null, methods returning T and the class itself, a generic method that uses T,
// inference from the declared type, and an instance inside another
class Stack<T> {
  items: T[] = [];
  push(x: T): void { this.items.push(x); }
  pop(): T { return this.items.pop()!; }
  get size(): int { return this.items.length; }
  map<U>(f: (x: T) => U): Stack<U> {
    const r = new Stack<U>();
    for (const x of this.items) r.push(f(x));
    return r;
  }
}
class Pair<A, B> {
  first: A;
  second: B;
  constructor(first: A, second: B) {
    this.first = first;
    this.second = second;
  }
  swap(): Pair<B, A> { return new Pair<B, A>(this.second, this.first); }
  show(): string { return `${this.first}/${this.second}`; }
}
class Box<T> {
  value: T | null = null;
  put(v: T): void { this.value = v; }
  has(): boolean { return this.value !== null; }
}
const ns = new Stack<number>();
ns.push(1.5); ns.push(2);
console.log(ns.size, ns.pop(), ns.size);
const ss: Stack<string> = new Stack();
ss.push("a"); ss.push("b");
const lens = ss.map((x) => x.length + 1);
console.log(ss.pop(), lens.pop(), lens.size);
const p = new Pair<string, int>("k", 3);
console.log(p.show(), p.swap().show(), p.swap().first + 1);
const b = new Box<Pair<string, int>>();
console.log(b.has());
b.put(p);
console.log(b.has(), b.value!.first);
const stacks = new Stack<Stack<int>>();
const inner = new Stack<int>();
inner.push(7);
stacks.push(inner);
console.log(stacks.pop().pop());
