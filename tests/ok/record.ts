// Record<K, V>: an object used as a dictionary (index, dot, in, delete, for-in, Object.keys/values/entries, JSON)
const m: Record<string, number> = { a: 1, b: 2 };
m["c"] = 3;
m.d = 4;
m.a += 10;
console.log(m["a"], m.b, m.c, Object.keys(m), Object.values(m));
console.log("a" in m, "z" in m);
delete m["b"];
for (const k in m) console.log(k, m[k]);
for (const [k, v] of Object.entries(m)) console.log(k, v);
console.log(m, JSON.stringify(m));
const hdr: Record<string, string> = {};
const key = "x-id";
hdr[key] = "7";
console.log(hdr[key] ?? "none", hdr["y"] ?? "none");
interface O { a: number; b?: string; }
const o: O = { a: 1 };
console.log("b" in o, "a" in o);
class P { x = 1; y = "q"; }
console.log(Object.keys(new P()));
class Req { headers: Record<string, string> = {}; }
const rq = new Req();
rq.headers["accept"] = "json";
rq.headers.host = "h";
console.log(rq.headers, Object.keys(rq.headers).length);
const parsed = JSON.parse('{"x":1,"y":2}') as Record<string, number>;
console.log(parsed.x + parsed["y"]);
const g = JSON.parse('{"a":{"n":1,"t":["x"]},"b":{"n":2,"t":[]},"a":{"n":3,"t":[]}}') as Record<string, { n: number; t: string[] }>;
console.log(g.a.n, g["b"].t.length, Object.keys(g), JSON.stringify(g));
const w: Record<string, Record<string, boolean>> = { x: { y: true } };
w["z"] = {};
w.z.q = false;
console.log(w, JSON.stringify(w));
