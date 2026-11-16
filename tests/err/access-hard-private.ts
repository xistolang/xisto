// expect: '#x' is private to the class that declares it
class A { #x = 1; }
const a = new A();
console.log(a.#x);
