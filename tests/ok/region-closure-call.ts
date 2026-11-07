// regions (region.c), found by an adversarial review: this must keep its objects alive past the iteration
class C {
  v: int;
  name: string;
  constructor(v: int) { this.v = v; this.name = `name${v}`; }
}
function mk(v: int): C { return new C(v); }
const f = (): C => mk(100);
const outside = f();
let s: int = 0;
for (let i: int = 0; i < 5; i++) {
  const c = mk(i);
  c.name = `overwritten-${i}`;
  s += c.v;
}
console.log(s, outside.v, outside.name);
