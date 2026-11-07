// expect: field 'x' is already declared in A
class A {
  x: int = 1;
}
class B extends A {
  x: int = 2;
}
console.log(new B().x);
