// regions (region.c), found by an adversarial review: this must keep its objects alive past the iteration
class A {
  v: int;
  constructor(v: int) { this.v = v; }
}
class B {
  w: int;
  name: string;
  constructor(w: int) { this.w = w; this.name = `b${w}`; }
}
function inner(k: int): int {
  let s: int = 0;
  for (let i: int = 0; i < 6; i++) {
    const b = new B(k * 1000 + i);
    s += b.w;
  }
  return s;
}
console.log(inner(0));
let total: int = 0;
for (let r: int = 0; r < 3; r++) {
  const arr: A[] = [];
  for (let j: int = 0; j < 12; j++) {
    arr.push(new A(j + 1));
    total += inner(j) % 7;
  }
  let t: int = 0;
  for (const a of arr) { t += a.v; }
  console.log(r, arr.length, t);
}
console.log(total);
