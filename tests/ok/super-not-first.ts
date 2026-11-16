// XB-153: statements before super(...) (no this there); the class field initializers run right after super(...), as in JavaScript
let order: string[] = [];
function note(s: string): number { order.push(s); return order.length; }
class Base { v: number; b0 = note("base field"); constructor(v: number) { note("base ctor"); this.v = v; } describe(): string { return "v=" + this.v; } }
class D extends Base {
  w: number;
  f = note("derived field");
  constructor(n: number) {
    const k = n * 2;
    note("before super " + k);
    if (k > 100) throw new Error("too big");
    super(k);
    note("after super");
    this.w = n;
  }
}
const d = new D(3);
console.log(d.v, d.w, d.f, d.b0, d.describe());
console.log(order.join(" | "));
try { new D(80); } catch (e) { console.log((e as Error).message); }
