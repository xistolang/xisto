// (u as C).x += 1 / -= / *= / /= with x a getter/setter pair, u an unknown: read through the getter, written through
// the setter (the cast read twice, no other effect); a plain field too (XB-234)
class C {
  private _x: number = 1;
  n: number = 5;
  get x(): number { return this._x; }
  set x(v: number) { this._x = v; console.log("set", v); }
}
const u: unknown = new C();
(u as C).x += 1;
(u as C).x -= 0.5;
(u as C).x *= 4;
(u as C).x = (u as C).x * 10;
console.log((u as C).x);
(u as C).n += 2;
(u as unknown as C).n *= 3;
(u as unknown as C).x += 3;
console.log((u as C).n, (u as C).x);
const w: unknown = new C();
(w as C).x /= 2;
console.log((w as C).x);
