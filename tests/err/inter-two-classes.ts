// expect: 'A & B' joins two classes with methods
class A { a = 1; f(): number { return this.a; } }
class B { b = 2; g(): number { return this.b; } }
type AB = A & B;
const x = JSON.parse('{"a":1,"b":2}') as AB;
console.log(x.a);
