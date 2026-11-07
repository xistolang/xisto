// expect: methods are not values
class A {
  f(): int {
    return 1;
  }
}
const a: A = new A();
const g: A = a.f;
