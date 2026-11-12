class A {
  x: int = 1;
}
class B extends A {
  x: int = 2;
}
console.log(new B().x);
const a: A = new B();
console.log(a.x, new A().x);
