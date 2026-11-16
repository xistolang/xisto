// f.call(obj, ...) / f.apply(obj, [...]): a function with a typed `this` takes obj as its object; another function
// ignores it (evaluated when it is a call); a method read as x.m or C.prototype.m runs on obj
interface P { name: string }
function greet(this: P, g: string, n: number): string { return `${g} ${this.name}${n}`; }
const p: P = { name: "ana" };
console.log(greet.call(p, "hi", 1));
console.log(greet.apply(p, ["yo", 2]));
function who(this: P): string { const f = (): string => this.name.toUpperCase(); return f(); }
console.log(who.call(p), who.apply({ name: "bia" }));
class C { k: number = 7; get(x: number): number { return this.k + x; } }
const c = new C();
const d = new C(); d.k = 100;
console.log(c.get.call(d, 1));
console.log(c.get.apply(c, [2]));
console.log(C.prototype.get.call(d, 3));
function sum(a: number, b: number): number { return a + b; }
console.log(sum.call(null, 1, 2), sum.apply(null, [3, 4]));
const add = (a: number, b: number): number => a + b;
console.log(add.call(null, 5, 6), add.apply(undefined, [7, 8]));
let n = 0;
function obj(): P { n++; return p; }
console.log(sum.call(obj(), 1, 1), n);
