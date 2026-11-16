// `this` in a static method is the class it was called on: a subclass calling an inherited static gets itself
// (a copy of the method per subclass), also across modules, with the base module's own helpers
import { Model } from "./lib/sthis/model.ts";
import { User } from "./lib/sthis/user.ts";
class A {
  static count = 0;
  static prefix = "#";
  static label(): string { return "A"; }
  static make(): string { A.count++; return this.prefix + this.label() + ":" + this.count; }
  static later(): () => string { return () => this.label() + "!"; }
  static create(): A { return new this(); }
  static twice(): string { return this.make() + "," + this.make(); }
  hello(): string { return "an A"; }
}
class B extends A {
  static prefix = "%";
  static label(): string { return "B"; }
  hello(): string { return "a B"; }
}
class C extends B {}
class Counter {
  static n = 0;
  static next(): number { return ++this.n; }
}
console.log(A.make());
console.log(B.make());
console.log(C.make());
console.log(A.later()(), B.later()(), C.later()());
console.log(A.create().hello(), B.create().hello(), C.create().hello());
console.log(C.twice());
console.log(A.count, B.count, C.count, C.prefix, C.label());
console.log(Counter.next(), Counter.next(), Counter.n);
console.log(Model.describe(), User.describe(), Model.build().name(), User.build().name());
