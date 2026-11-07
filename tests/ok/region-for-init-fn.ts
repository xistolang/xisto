// regions (region.c), found by an adversarial review: this must keep its objects alive past the iteration
class C {
  v: int;
  name: string;
  constructor(v: int) { this.v = v; this.name = `n${v}`; }
}
const kept: C[] = [];
function keep(c: C): int { kept.push(c); return 0; }
function run(k: int): void {
  for (let i: int = keep(new C(100 + k)); i < 3; i++) {
    const c = new C(i);
    c.name = `overwrite-${i}-${k}`;
  }
}
run(1);
run(2);
run(3);
for (const c of kept) { console.log(c.v, c.name); }
