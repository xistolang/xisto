// Classes printed only through another type (find_printable in emit.c): a field, an array, a map, a union, a
// subclass through its base, a generic function's instance, a value passed as unknown.
class Leaf {
  v: number = 1;
}
class Holder {
  leaf: Leaf = new Leaf();
  name: string = "h";
}
class InMap {
  k: string = "m";
}
class A {
  a: number = 1;
}
class B {
  b: string = "b";
}
class Base {
  x: number = 0;
}
class Sub extends Base {
  y: number = 2;
}
class Gen {
  g: boolean = true;
}
class Unk {
  u: number = 9;
}
class Never {
  n: number = 3;
}
function show<T>(x: T): void {
  console.log(x);
}
function pass(v: unknown): string {
  return typeof v;
}
console.log(new Holder());
const m = new Map<string, InMap>();
m.set("one", new InMap());
console.log(m);
let u: A | B = new A();
if (Math.random() < 2) u = new B();
console.log(u);
const b: Base = new Sub();
console.log([b]);
show(new Gen());
console.log(pass(new Unk()));
const nv = new Never();
console.log(nv.n);
