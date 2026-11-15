// build: --profile
// --profile: early returns in closures, a generator body, try/finally, async and a constructor compile (XB-227)
class Tracer {
  private n: int = 0;
  name: string;
  constructor(name: string) {
    this.name = name;
    if (name === "") return;
    this.n = 1;
  }
  span(x: int): int {
    try {
      if (x > 2) return x * 2;
      for (let i = 0; i < 3; i++) { if (i === x) return i; }
    } finally {
      this.n++;
    }
    return -1;
  }
  async flush(x: int): Promise<int> {
    if (x === 0) return 0;
    await new Promise<void>((r) => setTimeout(r, 1));
    if (x === 1) { return 1; }
    return this.n;
  }
  *gen(x: int): Generator<int> {
    if (x === 0) return;
    yield x;
  }
}
const pick = (x: int): string => {
  if (x === 0) { return "zero"; }
  const inner = (y: int): int => { if (y > 1) return y; return 0; };
  return "n" + inner(x);
};
function f(x: int): int {
  const g = function (y: int): int { if (y) { return 1; } return 2; };
  switch (x) { case 1: return g(x); default: break; }
  return 0;
}
async function main(): Promise<void> {
  const t = new Tracer("a");
  console.log(t.span(1), t.span(5), pick(0), pick(3), f(1), f(2));
  console.log(await t.flush(0), await t.flush(1), await t.flush(2));
  for (const v of t.gen(3)) console.log(v);
  const xs = [3, 1, 2].map((v) => { if (v > 2) return v; return -v; });
  console.log(xs.join(","));
  const fs = [1, 2].filter((v) => v > 1);
  console.log(fs.length, new Tracer("").name.length);
}
main();
