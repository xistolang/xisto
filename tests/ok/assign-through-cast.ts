// XB-221: assignment through a cast, (x as T).field = v: a field, a setter, ++ and op=, on an unknown, an any,
// a base-class value and an element of an unknown[]
interface Span { name: string; n: number; tags: string[] }
class Base { kind = "base"; widen(): void { (this as unknown as Sub).extra = 100; } }
class Sub extends Base { extra = 0; }
class Acc { private _x = 0; get x(): number { return this._x; } set x(v: number) { this._x = v * 2; } }
const real: Span = { name: "x", n: 1, tags: [] };
const anyObj: any = real;
(anyObj as Span).name = "y";
(anyObj as Span).n += 2;
console.log(real.name, real.n);
const u: unknown = real;
(u as Span).n = 5;
(u as Span).tags.push("t");
console.log((u as Span).n, (u as Span).tags);
const b: Base = new Sub();
(b as Sub).extra = 7;
(b as Sub).extra++;
console.log((b as Sub).extra);
b.widen();
console.log((b as Sub).extra);
let sp: Span | null = { name: "s", n: 0, tags: [] };
(sp as Span).name = "nn";
(sp!).n = 3;
console.log(sp);
const arr: unknown[] = [real];
(arr[0] as Span).name = "arr";
console.log(real.name);
const acc = new Acc();
const ua: unknown = acc;
(ua as Acc).x = 5;
console.log(acc.x);
(acc as Acc).x = 6;
console.log(acc.x);
