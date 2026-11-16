// XB-173: a function returning a primitive inside a union (held returning it boxed): called after narrowing, in a field too
function f(x: number | (() => number)): number {
  return typeof x === "number" ? x : x();
}
console.log(f(1), f(() => 2));
interface Cfg { size: number | ((k: number) => number); on: boolean | (() => boolean) }
function size(c: Cfg): number {
  return typeof c.size === "function" ? c.size(10) : c.size;
}
function on(c: Cfg): boolean {
  if (typeof c.on === "boolean") return c.on;
  const read = c.on;
  return read();
}
const a: Cfg = { size: 3, on: true };
const b: Cfg = { size: (k) => k * 2, on: () => false };
console.log(size(a), size(b), on(a), on(b));
console.log(a, b);
console.log(JSON.stringify(a), JSON.stringify(b));
