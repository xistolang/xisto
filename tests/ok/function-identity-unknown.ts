// A function kept as an unknown, any or object keeps its identity (the 0.4.3 regression: each conversion made a new
// adapter, so Map keys, Set members, === and reflect-metadata targets lost it): with and without captures, a
// top-level function, a const arrow and a closure, a primitive result or not
const fn = (x: number): number => x * 2;
function decl(): number { return 1; }
function makeAdder(k: number): (x: number) => number { return (x: number): number => x + k; }
const add3 = makeAdder(3);
const greet = (s: string): string => "hi " + s;

const m = new Map<object, string>();
m.set(fn as object, "arrow");
m.set(decl as object, "decl");
m.set(add3 as object, "closure");
m.set(greet as object, "string result");
console.log(m.get(fn as object) ?? "lost", m.get(decl as object) ?? "lost", m.get(add3 as object) ?? "lost", m.get(greet as object) ?? "lost", m.size);

function keep(t: unknown): unknown { return t; }
console.log(keep(fn) === keep(fn), keep(fn) !== keep(fn), keep(add3) === keep(add3), keep(fn) === keep(decl));

const a: any = fn;
const b: any = fn;
const c: any = add3;
console.log(a === b, a === c);

const s = new Set<unknown>();
s.add(fn);
s.add(fn);
s.add(add3);
console.log(s.size, s.has(fn), s.has(decl));

const back = keep(add3) as (x: number) => number;
console.log(back(4), (a as (x: number) => number)(5));
