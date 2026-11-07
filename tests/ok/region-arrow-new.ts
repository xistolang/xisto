// regions (region.c), found by an adversarial review: this must keep its objects alive past the iteration
class C {
  v: int;
  name: string;
  constructor(v: int) { this.v = v; this.name = `name${v}`; }
}
const make = (v: int): C => new C(v);
const outside = make(100);
let s: int = 0;
for (let i: int = 0; i < 5; i++) {
  const c = new C(i);
  c.name = `overwritten-${i}`;
  s += c.v;
}
console.log(s, outside.v, outside.name);
