// regions (region.c), found by an adversarial review: this must keep its objects alive past the iteration
class C {
  v: int;
  constructor(v: int) { this.v = v; }
}
function run(k: int): int {
  const before: C[] = [];
  const tags: string[] = [];
  for (let i: int = 0; i < 3; i++) {
    const loc: C[] = [];
    loc.push(new C(i + k));
    tags.push(`t${loc[0].v}`);
  }
  const after: C[] = [];
  for (let j: int = 0; j < 3; j++) {
    tags.push(`pad-${j}-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx`);
  }
  return before.length + after.length + tags.length;
}
console.log(run(1));
console.log(run(2));
