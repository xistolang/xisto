// JSON.parse of values nested thousands of levels deep (the limit was 200), read back as unknown, any and a typed
// recursive interface (XB-234)
const depth = 9000;
const s = "[".repeat(depth) + "]".repeat(depth);
const v: unknown = JSON.parse(s);
console.log(JSON.stringify(v).length);
console.log(v);
const a: any = JSON.parse(s);
console.log(JSON.stringify(a).length, Array.isArray(a));
const o = '{"a":'.repeat(depth) + "1" + "}".repeat(depth);
const w: unknown = JSON.parse(o);
console.log(JSON.stringify(w).length);
console.log(w);
interface N { a: N | number }
const n: N = JSON.parse('{"a":'.repeat(300) + "1" + "}".repeat(300));
function dep(x: N | number): number { return typeof x === "number" ? 0 : 1 + dep(x.a); }
console.log(dep(n));
