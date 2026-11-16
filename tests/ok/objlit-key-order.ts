// XB-169: an object literal typed by an interface keeps its keys in the literal's order (console.log, JSON.stringify,
// Object.keys/entries), as JavaScript does; keys set later come after them
interface P { a: number; b: string; c: boolean }
const p: P = { c: true, a: 1, b: "x" };
console.log(p);
console.log(JSON.stringify(p));
const base = { b: "y", c: false };
const q: P = { ...base, a: 2 };
console.log(q, JSON.stringify(q));
const same: P = { a: 3, b: "s", c: true };
console.log(same, JSON.stringify([p, same, q]));
interface Addr { city: string; zip?: string; n: number }
interface User { id: number; name: string; addr: Addr; tags: string[]; nick?: string | null }
const u: User = { name: "ana", tags: ["a"], id: 1, addr: { n: 3, city: "rec" } };
console.log(u);
console.log(JSON.stringify(u));
const us: User[] = [u, { id: 2, name: "b", addr: { city: "x", zip: "1", n: 1 }, tags: [] }];
console.log(us, JSON.stringify(us));
u.addr.zip = "999";
console.log(u.addr, JSON.stringify(u.addr));
const m = new Map<string, Addr>();
m.set("k", { zip: "0", n: 0, city: "c" });
console.log(m);
const anyv: unknown = u;
console.log(JSON.stringify(anyv));
console.log(Object.keys(u), Object.entries(u.addr));
const copy = structuredClone(u);
console.log(copy, JSON.stringify(copy));
function mk(): Addr { return { n: 7, city: "f" }; }
console.log(mk(), JSON.stringify({ a: mk() }));
