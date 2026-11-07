// expect: it is filled from JSON, not constructed
class A {
  name: string;
}
const a = new A();
console.log(a.name);
