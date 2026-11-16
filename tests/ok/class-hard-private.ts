// #private fields, methods, accessors and statics: reachable only inside their class, left out of console.log,
// JSON.stringify, Object.keys/values/entries and object spreads (as on Node), `#x in o` as a brand check, and a
// subclass's #x apart from its base's #x of the same name.
class Plain { name = "p"; }
class Counter {
  #count = 0;
  static #instances = 0;
  name = "c";
  #tags: string[] = ["x"];
  constructor() { Counter.#instances++; }
  #bump(): void { this.#count++; }
  inc(): number { this.#bump(); return this.#count; }
  static has(o: Counter | Plain): boolean { return #count in o; }
  static total(): number { return Counter.#instances; }
  get #secret(): string { return `s${this.#count}${this.#tags.length}`; }
  reveal(): string { return this.#secret; }
  same(other: Counter): boolean { return other.#count === this.#count; }
}
class Sub extends Counter { extra = 1; }
const c = new Counter();
c.inc(); c.inc();
console.log(c.inc(), Counter.total(), Counter.has(c), Counter.has(new Plain()), c.reveal(), c.same(new Counter()));
console.log(c);
console.log(JSON.stringify(c));
console.log(Object.keys(c), Object.entries(c));
const s = new Sub();
console.log(s, JSON.stringify(s), Counter.has(s), s.inc());
console.log([c], { c });

class Base { #id = 1; name = "b"; baseId(): number { return this.#id; } }
class Derived extends Base { #id = 2; subId(): number { return this.#id; } }
const d = new Derived();
console.log(d.baseId(), d.subId(), d);
const copy = { ...d };
console.log(copy, Object.values(d));
class Only { #a = 1; #b = "x"; get a(): number { return this.#a + this.#b.length; } }
console.log(new Only(), JSON.stringify(new Only()), new Only().a);
