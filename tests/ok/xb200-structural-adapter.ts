// XB-200: a class that has the fields and methods of an interface, without `implements`, used where the interface
// goes: its fields read and written through the interface reach the object itself (they were read out of the adapter)
interface Named {
  name: string;
  count: number;
  tags: string[];
  hello(): string;
}
class Dog {
  age = 3; // a field the interface does not have, before the ones it has
  tags: string[] = ["a"];
  name = "rex";
  count = 1;
  hello(): string { return "woof " + this.name + " " + this.count; }
}
class Puppy extends Dog {
  small = true;
}
class Cat implements Named {
  name = "tom";
  count = 10;
  tags: string[] = [];
  hello(): string { return "meow " + this.name; }
}
function greet(x: Named): string { return x.name + " says " + x.hello(); }
function bump(x: Named): void {
  x.count += 2;
  x.count++;
  x.name = x.name + "!";
  x.tags.push("t" + x.count);
}
const d = new Dog();
console.log(greet(d));
const n: Named = d;
bump(n);
console.log(n.name, n.count, n.tags.join(","), d.name, d.count, d.tags.length);
d.name = "max";
d.count = 7;
console.log(n.name, n.count, n.hello());
console.log(JSON.stringify(n));
const all: Named[] = [new Dog(), new Cat(), new Puppy()];
for (const a of all) {
  bump(a);
  console.log(greet(a), a.count, a.tags.join("|"));
}
let total = 0;
for (const a of all) total += a.count;
console.log(total);
function rename(x: Named, to: string): Named { x.name = to; return x; }
console.log(rename(new Puppy(), "fido").name);
console.log(greet(new Dog()));
