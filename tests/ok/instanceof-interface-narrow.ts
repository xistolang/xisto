// XB-166: x instanceof C narrows x of an interface (or base class) type to C, in the branch and after a negated early return
interface Shape { area(): number; }
class Sq implements Shape { s: number; constructor(s: number) { this.s = s; } area(): number { return this.s * this.s; } side(): number { return this.s; } }
class Ci implements Shape { r: number; constructor(r: number) { this.r = r; } area(): number { return 3 * this.r * this.r; } }
class Base { k = 1; }
class Der extends Base { extra = "d"; }
function f(x: Shape): number { if (x instanceof Sq) return x.side(); return x.area(); }
function g(x: Shape | null): string {
  if (x instanceof Ci) return "ci " + x.r;
  if (!(x instanceof Sq)) return "other";
  return "sq " + x.s;
}
function h(b: Base): string { return b instanceof Der ? b.extra : "base " + b.k; }
console.log(f(new Sq(3)), f(new Ci(2)));
console.log(g(new Sq(4)), g(new Ci(5)), g(null));
console.log(h(new Der()), h(new Base()));
const shapes: Shape[] = [new Sq(2), new Ci(1)];
for (const s of shapes) { if (s instanceof Sq) console.log("side", s.side()); else console.log("area", s.area()); }
