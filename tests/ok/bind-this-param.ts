// f.bind(obj, ...args) of a function declared with this: T
function f(this: any, a: number) { return this.x + a; }
console.log(f.call({ x: 1 }, 2), f.apply({ x: 2 }, [3]));
const g = f.bind({ x: 3 });
console.log(g(4), [1, 2].map(g));
const h = f.bind({ x: 5 }, 6);
console.log(h());
interface Pt { x: number; y: number }
function norm(this: Pt, scale: number, label: string): string { return label + ":" + (this.x * this.x + this.y * this.y) * scale; }
let made = 0;
function mk(): Pt { made++; return { x: 1, y: 2 }; }
const n = norm.bind(mk(), 10);
console.log(n("a"), n("b"), made);
const n2 = norm.bind({ x: 3, y: 4 });
console.log(n2(1, "c"));
function greet(this: { name: string }, ...rest: string[]): string { return this.name + " " + rest.join(","); }
const gr = greet.bind({ name: "ana" }, "x");
console.log(gr("y", "z"));
