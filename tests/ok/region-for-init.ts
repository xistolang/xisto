// regions (region.c), found by an adversarial review: this must keep its objects alive past the iteration
class C {
  v: int;
  constructor(v: int) { this.v = v; }
}
const kept: C[] = [];
function keep(c: C): int { kept.push(c); return 0; }
for (let i: int = keep(new C(99)); i < 3; i++) {
  const c = new C(i);
  console.log(c.v);
}
for (let i: int = 0; i < 3; i++) { const s = `junk-${i}-xxxxxxxxxxxxxxxxxxxxx`; console.log(s.length); }
console.log(kept.length, kept[0].v);
