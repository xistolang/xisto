// [new A(), new B()] with no type written: (A | B)[], each element keeping its class (instanceof, printing);
// x.constructor.name with x of a union of classes; console.log of an object value (XB-235)
class A { a = 1; }
class B { b = 2; }
const ys = [new A(), new B()];
console.log(ys.length);
console.log(ys[0] instanceof A, ys[1] instanceof B, ys[0] instanceof B);
console.log(ys);
class Animal { name: string; constructor(name: string) { this.name = name; } speak(): string { return this.name; } }
class Dog extends Animal { bark(): string { return "woof"; } }
class Cat extends Animal { cb: () => string = () => "meow"; }
const zs = [new Dog("rex"), new Cat("tom")];
for (const z of zs) console.log(z.name, z.speak(), z instanceof Dog, z instanceof Cat);
console.log(zs.map((z) => z.constructor.name).join(","));
const ws = [new A(), new B(), new A()];
ws.push(new B());
console.log(ws.filter((w) => w instanceof A).length, ws);
class B2 extends B { c = 3; }
function nm(x: A | B): string { return x.constructor.name; }
console.log(nm(new A()), nm(new B()), nm(new B2()));
const u: A | B = new B();
console.log(`${u.constructor.name}!`);
const o: object = { k: [1, "x"], n: null };
console.log(o);
