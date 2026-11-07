// regions (region.c), found by an adversarial review: this must keep its objects alive past the iteration
class C {
  v: int;
  constructor(v: int) { this.v = v; }
}
const before: C[] = [];
let s: int = 0;
for (let i: int = 0; i < 4; i++) {
  const c = new C(i + 1000);
  s += c.v;
}
console.log(s, before.length);
