// expect: the generic method 'f' is not a value
class A {
  f<T>(x: T): T {
    return x;
  }
}
const a: A = new A();
const g = a.f;
