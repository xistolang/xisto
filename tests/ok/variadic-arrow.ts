class A { n = 1; }
class B { s = "b"; }
class FP {
  provide: unknown;
  useFactory: (...args: any[]) => unknown;
  inject?: unknown[];
}
const p: FP = { provide: "X", useFactory: (a: A, b: B) => a.n + b.s, inject: ["A", "B"] };
const r = p.useFactory(new A(), new B());
console.log(r);
const q: FP = { provide: "Y", useFactory: () => 42 };
console.log(q.useFactory());
type Cb = (...xs: unknown[]) => string;
function call(f: Cb, xs: unknown[]): string { return f(...xs); }
console.log(call((s: string, n: number) => s + ":" + n, ["k", 3]));
const g: Cb = (x) => String(x);
console.log(g(7, 8));
