// namespace N { ... }: N.name in expressions, types, new and instanceof; nested and dotted namespaces, merged bodies,
// an enum merged with a namespace; declare namespace is types only (Node cannot run namespaces: the oracle is Bun)
namespace Geo {
  export const unit = "m";
  let count = 0;
  export interface Pt { x: number; y: number }
  export type Pair = [number, number];
  function sq(n: number): number { count++; return n * n; }
  export function dist(a: Pt, b: Pt): number { return Math.sqrt(sq(a.x - b.x) + sq(a.y - b.y)); }
  export function calls(): number { return count; }
  export class Box {
    unit = "box";
    constructor(public w: number, public h: number) {}
    area(): number { return this.w * this.h; }
    label(): string { return `${this.area()}${unit}2`; }
  }
  export enum Kind { Small, Big }
  export namespace Inner {
    export const unit = "cm";
    export function both(): string { return unit + "/" + Geo.unit; }
  }
  export function make(k: Kind): Box { return k === Kind.Big ? new Box(10, 10) : new Box(1, 2); }
  const cfg = { unit: "obj", dist: 3 };
  export function cfgUnit(): string { return cfg.unit + cfg.dist; }
}
namespace Geo {
  export function twice(): number { return dist({ x: 0, y: 0 }, { x: 3, y: 4 }) * 2; }
}
namespace A.B.C {
  export const deep = "deep";
}
enum Color { Red, Green }
namespace Color {
  export function parse(s: string): Color { return s === "green" ? Color.Green : Color.Red; }
}
const p: Geo.Pt = { x: 0, y: 0 };
const q: Geo.Pt = { x: 6, y: 8 };
console.log(Geo.unit, Geo.dist(p, q), Geo.calls());
const b: Geo.Box = new Geo.Box(2, 3);
console.log(b.area(), b.label(), b.unit);
console.log(Geo.make(Geo.Kind.Big).area(), Geo.Kind.Small, Geo.Kind[1]);
console.log(Geo.Inner.unit, Geo.Inner.both(), Geo.twice(), Geo.cfgUnit());
console.log(A.B.C.deep, Color.parse("green"), Color.Red, Color[1]);
const pr: Geo.Pair = [1, 2];
console.log(pr, Geo.dist.name, typeof Geo.dist);
declare namespace Ext { interface I { a: number } }
namespace N2 {
  export function f(): number { return 1; }
  export class K { static make(): K { return new K(); } }
  export const arrow = (x: number): number => x + 1;
}
console.log(N2.f.name, N2.K.name, N2.f, N2.arrow(1));
console.log(new N2.K() instanceof N2.K);
try { throw new Error("x"); } catch (e) { console.log((e as Error).message); }
