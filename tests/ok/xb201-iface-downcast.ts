// XB-201: i as C, with i an interface value that holds an object of class C that fits the interface without
// `implements` (held through an adapter): the object itself, as in JavaScript (it was a segmentation fault)
interface Named { name: string; hello(): string }
interface Greeter { hello(): string }
class Dog {
  name = "rex";
  hello(): string { return "woof " + this.name; }
}
class Puppy extends Dog {
  size = 1;
}
const d = new Dog();
const n: Named = d;
console.log((n as Dog) === d, (n as Dog).name);
(n as Dog).name = "max";
console.log(d.name, n.name);
const g: Greeter = d;
console.log((g as Dog) === d, (g as Dog).hello());
const p = new Puppy();
const pn: Named = p;
console.log((pn as Dog) === p, (pn as Puppy).size, (pn as Puppy) === p);
function back(x: Named | null): Dog | null { return x as Dog | null; }
console.log(back(n) === d, back(null));
const list: Greeter[] = [new Dog(), new Puppy()];
for (const x of list) console.log((x as Dog).name, x.hello());
// a fresh value cast down: the temporary is let go (it leaked, through an adapter or not)
function mk(s: string): Named { const x = new Dog(); x.name = s; return x; }
console.log((mk("a") as Dog).name);
let keep: Dog | null = null;
for (let i = 0; i < 3; i++) keep = mk("c" + i) as Dog;
console.log(keep!.name);
class Base { b = 0; }
class Sub extends Base { x = 1; }
function mkb(): Base { return new Sub(); }
console.log((mkb() as Sub).x);
