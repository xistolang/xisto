// JSON.stringify(value, replacer, space) as Node writes it: indentation by a number (clamped to 10) or a string (its first
// 10 characters), nested objects and arrays, empty {} and [] compact, unicode, toJSON and Date with indentation, a key
// allow-list (every level, its order, duplicates once) and a function replacer (root key "", undefined drops a member
// and is null in an array, null stays null, a new object returned is walked too)
class Inner { n: number; tags: string[]; constructor(n: number, tags: string[]) { this.n = n; this.tags = tags; } }
class P {
  x: number; name: string; inner: Inner; empty: number[]; e2: Inner[]; when: Date; opt?: string;
  constructor(x: number, name: string) { this.x = x; this.name = name; this.inner = new Inner(2, ["a", "b"]); this.empty = []; this.e2 = []; this.when = new Date(0); }
}
const p = new P(1, "ã \"q\"   😀");
console.log(JSON.stringify(p, null, 2));
console.log(JSON.stringify(p, null, "\t"));
console.log(JSON.stringify(p, null, 0));
console.log(JSON.stringify(p, null, 20));
console.log(JSON.stringify(p, null, "--------------xx"));
console.log(JSON.stringify(p, undefined, 1));
console.log(JSON.stringify(p, ["name", "inner", "n", "x", "name"]));
console.log(JSON.stringify(p, ["inner", "tags"], 2));
console.log(JSON.stringify([[1, 2], [3], [] as number[]], null, 2));
console.log(JSON.stringify(p, (k, v) => k === "name" ? undefined : v));
console.log(JSON.stringify(p, (k: string, v: any) => typeof v === "number" ? v * 10 : v, 2));
console.log(JSON.stringify([1, "a", true], (k, v) => k === "1" ? undefined : v));
console.log(JSON.stringify(p, (k, v) => k === "inner" ? null : v));
console.log(JSON.stringify(p, (k, v) => k === "" ? 5 : v));
console.log(JSON.stringify("str", null, 2), JSON.stringify(42, null, 2), JSON.stringify([] as string[], null, 2), JSON.stringify({}, null, 2));
class T { v: number; constructor(v: number) { this.v = v; } toJSON(): string { return "T" + this.v; } }
console.log(JSON.stringify([new T(1), new T(2)], null, 2));
console.log(JSON.stringify({ t: new T(3), d: new Date(86400000) }, (k, v) => k === "t" ? "replaced:" + v : v, 2));
class U { id: number; password: string; email: string | null; roles: string[]; constructor() { this.id = 7; this.password = "s3"; this.email = null; this.roles = ["a"]; } }
function hide(key: string, value: any): any {
  if (key === "password") return undefined;
  return value;
}
const u = new U();
console.log(JSON.stringify(u, hide, 2));
const keys: string[] = ["id", "email"];
console.log(JSON.stringify(u, keys));
console.log(JSON.stringify([u, u], keys, 1));
const seen: string[] = [];
console.log(JSON.stringify(u, (k, v) => { seen.push(k); return v; }));
console.log(seen.join("|"));
console.log(JSON.stringify(u, (k, v) => k === "roles" ? { replaced: true, n: [1, 2] } : v, 2));
console.log(JSON.stringify(u, (k: string, v: unknown): unknown => v));
const sp = 3;
console.log(JSON.stringify({ a: [{ b: 1 }] }, null, sp));
console.log(JSON.stringify({ a: "x" }, null, ""));
console.log(JSON.stringify({ a: "x" }, null, 3.7));
console.log(JSON.stringify({ a: "x" }, null, -1));
const rec: Record<string, unknown> = { a: 1, b: "two", c: [true, false], n: null, d: { e: [] as number[] } };
console.log(JSON.stringify(rec, null, 2));
console.log(JSON.stringify(rec));
// a Map or a Set the replacer returns has no properties of its own: {}
const mp = new Map<string, number>();
mp.set("a", 1);
console.log(JSON.stringify(u, (k, v) => k === "roles" ? mp : k === "email" ? new Set<string>(["x"]) : v, 2));
// the root replaced, dropped
console.log(JSON.stringify(u, (k, v) => k === "" ? [1, "a"] : v, 2));
console.log(JSON.stringify(u, (k, v) => k === "" ? undefined : v));
