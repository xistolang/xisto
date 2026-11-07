// Inheritance across modules and levels: overriding in the middle, a base calling an overridden method,
// initialization order, parameter properties after super, interface extends, statics from another module.
import { Base, Registry } from "./lib/shapes.ts";

class Middle extends Base {
  extra: string = "middle field";
  constructor(id: int) {
    super(id);
    this.log.push(`Middle constructor (extra = ${this.extra})`);
  }
  name(): string {
    return "middle";
  }
}

class Leaf extends Middle {
  constructor(id: int, private readonly tag: string) {
    super(id);
    this.log.push(`Leaf constructor (tag = ${this.tag})`);
  }
  greet(): string {
    return super.greet() + " (leaf " + this.tag + ")";
  }
}

class Plain extends Base {}

interface Named {
  name: string;
}
interface Person extends Named {
  age: int;
}
interface Employee extends Person {
  company: string;
}

const items: Base[] = [new Base(1), new Middle(2), new Leaf(3, "x"), new Plain(4)];
for (const b of items) {
  console.log(b.greet(), b.name(), b instanceof Middle, b instanceof Base);
}
const leaf = new Leaf(5, "y");
console.log(leaf.log.join(" > "));
console.log("created:", Registry.created, Registry.label);

let maybe: Base | null = null;
console.log(maybe === null);
maybe = new Leaf(6, "z");
if (maybe !== null) {
  console.log(maybe.greet());
}

const e: Employee = { name: "ana", age: 30, company: "acme" };
const p: Person = { name: "bia", age: 25 };
console.log(e.name, e.age, e.company, p.name, JSON.stringify(e));
const back: Employee = JSON.parse(`{"name":"caio","age":41,"company":"x"}`);
console.log(JSON.stringify(back));

const middles: Middle[] = [new Middle(7), new Leaf(8, "w")];
function names(list: Base[]): string {
  return list.map((b: Base): string => b.name()).join(",");
}
console.log(names(middles));
