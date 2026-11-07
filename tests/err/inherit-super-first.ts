// expect: the constructor of B must call super(...) first
class A {
  x: int;
  constructor(x: int) {
    this.x = x;
  }
}
class B extends A {
  constructor() {
    console.log("before");
    super(1);
  }
}
console.log(new B().x);
