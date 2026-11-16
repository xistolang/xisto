// const N = class Named { ... }: the class Named (N.name is "Named", Named inside the body), N another name for it;
// class expressions with a base inside a function.
const K = class { v = 1; get(): number { return this.v; } };
const N = class Named {
  w = 2;
  who(): string { return Named.name; }
  static make(): Named { return new Named(); }
};
console.log(new K().get(), new N().who(), N.name, K.name, N.make(), new N() instanceof N);
class Base { hello(): string { return "base"; } }
const Shout = class Loud extends Base { shout(): string { return this.hello().toUpperCase(); } };
const s = new Shout();
console.log(s.shout(), s instanceof Base, Shout.name, s);
