// Methods as values: o.m.bind(o) (on and off of an EventEmitter with the same bound function, two binds are two
// functions), bind with partial arguments on a function, an arrow and a method, a subclass override, a value read
// before the variable and the object change, call and apply, static methods as values, f.name and f.length
import { EventEmitter } from "node:events";

class Base {
  tag: string;
  constructor(tag: string) { this.tag = tag; }
  describe(x: number): string { return "base " + this.tag + " " + x; }
  static make(t: string): Base { return new Base(t); }
}
class Derived extends Base {
  describe(x: number): string { return "derived " + this.tag + " " + (x * 2); }
}

class Feed {
  name: string;
  got: string[];
  em: EventEmitter;
  constructor(name: string) {
    this.name = name;
    this.got = [];
    this.em = new EventEmitter();
  }
  onData(chunk: string): void { this.got.push(this.name + ":" + chunk); }
  start(): void {
    const h = this.onData.bind(this);
    this.em.on("data", h);
    this.em.emit("data", "a");
    this.em.emit("data", "b");
    this.em.off("data", h);
    this.em.emit("data", "c");
    const h2 = this.onData.bind(this);
    this.em.on("data", h2);
    this.em.off("data", this.onData.bind(this)); // another function: nothing removed
    this.em.emit("data", "d");
    this.em.removeAllListeners("data");
  }
  fmt(n: number): string { return this.name + "#" + n; }
  all(): string[] { return [1, 2, 3].map((n) => n * 10).map(this.fmt.bind(this)); }
}

function add3(a: number, b: number, c: number): number { return a * 100 + b * 10 + c; }
const add = (a: number, b: number): number => a + b;

const s = new Feed("srv");
s.start();
console.log(s.got.join(","));
console.log(s.all().join(","));

// bind with partial arguments, on a function, an arrow and a method
const p1 = add3.bind(null, 1);
const p2 = add3.bind(null, 1, 2);
console.log(p1(2, 3), p2(4), add.bind(null, 5)(6));
const bd = new Base("x").describe.bind(new Base("y"), 7);
console.log(bd());

// the subclass's override, through a variable of the base type
const objs: Base[] = [new Base("b"), new Derived("d")];
const ds: ((n: number) => string)[] = [objs[0].describe.bind(objs[0]), objs[1].describe.bind(objs[1])];
console.log(ds[0](1), ds[1](1));

// captured, then the variable and the object change: the value keeps the object it was read from
let o: Base = new Base("first");
const bound = o.describe.bind(o);
const first = o;
o = new Base("second");
first.tag = "first*";
console.log(bound(3), o.describe(3));

// call and apply
console.log(new Derived("q").describe.call(new Derived("r"), 4));
console.log(add3.call(null, 1, 2, 3), add3.apply(null, [4, 5, 6]));

// static methods as values
const mk = Base.make;
console.log(mk("st").describe(1));

// name and length
console.log(add3.name, add3.length, add.length, s.fmt.name, s.fmt.length);

// identity: one bound function is itself, two binds are two functions
const one = s.fmt.bind(s);
const same = one;
console.log(one === same, s.fmt.bind(s) === s.fmt.bind(s));

// a bound method as a callback parameter
function twice(f: (n: number) => string): string { return f(1) + "|" + f(2); }
console.log(twice(s.fmt.bind(s)));
