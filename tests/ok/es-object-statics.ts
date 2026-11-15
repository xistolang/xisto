// Object.assign, fromEntries, freeze, defineProperty, getOwnPropertyNames, hasOwn, is, and keys/values/entries of
// class instances
class Base {
  id = 7;
}
class Point extends Base {
  x = 1;
  y = 2;
  label = "p";
}
console.log(Object.entries(new Point()));
console.log(Object.keys(new Point()), Object.values(new Point()));
const p = new Point();
console.log(Object.entries(p).map(([k, v]) => k + "=" + String(v)).join("&"));
console.log(Object.entries({ a: 1, b: "two" }), Object.keys({ z: true }));
// assign: several sources, later ones win
const target: Record<string, number> = { a: 1, b: 2 };
const out = Object.assign(target, { b: 3, c: 4 }, { c: 5, d: 6 });
console.log(out, target, out === target);
const extra: Record<string, number> = { e: 9, a: 0 };
Object.assign(target, extra);
console.log(target);
class Opts {
  verbose = false;
  depth = 1;
  name = "x";
}
const o = new Opts();
Object.assign(o, { depth: 3 }, { name: "y", verbose: true });
console.log(o);
// fromEntries
const m = new Map<string, number>([["one", 1], ["two", 2]]);
console.log(Object.fromEntries(m), Object.fromEntries(m.entries()));
const pairs: [string, string][] = [["k", "v"], ["k2", "v2"]];
console.log(Object.fromEntries(pairs));
// freeze, seal: the object itself
const frozen = Object.freeze({ a: 1, list: [1, 2] });
console.log(frozen, Object.seal(new Opts()).depth, Object.preventExtensions(p).x);
// defineProperty / defineProperties with a value
const rec: Record<string, number> = {};
Object.defineProperty(rec, "answer", { value: 42, writable: true, enumerable: true, configurable: true });
Object.defineProperties(rec, { a: { value: 1, enumerable: true }, b: { value: 2, enumerable: true } });
console.log(rec);
const pt = new Point();
Object.defineProperty(pt, "x", { value: 10, writable: true });
console.log(pt.x);
// getOwnPropertyNames, hasOwn, is
console.log(Object.getOwnPropertyNames(rec), Object.getOwnPropertyNames(new Opts()));
console.log(Object.hasOwn(rec, "answer"), Object.hasOwn(rec, "nope"), Object.hasOwn(pt, "label"));
console.log(Object.is(NaN, NaN), Object.is(0, -0), Object.is(-0, -0), Object.is(1, 1), Object.is(NaN, 0));
console.log(Object.is("a", "a"), Object.is("a", "b"), Object.is(p, p), Object.is(p, pt), NaN === NaN);
