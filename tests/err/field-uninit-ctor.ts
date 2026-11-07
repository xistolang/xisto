// expect: never initialized
class B {
  name: string;
  constructor(x: int) {
    console.log(x);
  }
}
console.log(new B(1).name);
