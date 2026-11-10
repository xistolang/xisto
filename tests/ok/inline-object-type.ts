// inline object types: { a: T; b?: U } as parameter, variable, element, return and field types
function f(o: { a: number; b: string }): string { return o.b + String(o.a); }
console.log(f({ a: 1, b: "x" }));
let q: { name: string; age?: number } = { name: "z" };
console.log(q.name, q.age);
const list: { id: int; tags: string[] }[] = [{ id: 1, tags: ["a"] }, { id: 2, tags: [] }];
for (const it of list) console.log(it.id, it.tags.length);
function g(): { ok: boolean; n: number } | null { return { ok: true, n: 2 }; }
const r = g();
if (r != null) console.log(r.ok, r.n);
console.log(JSON.stringify(list), JSON.stringify(q));
class Holder { cfg: { host: string; port: number } = { host: "h", port: 1 }; }
console.log(new Holder().cfg.port);
