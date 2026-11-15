// a value of type T | undefined (no null in the union) that is empty prints as undefined, as JavaScript's value does
// (xisto holds it as null, its one empty value); T | null | undefined stays null (XB-234)
function f(k: number): number | undefined {
  if (k > 0) return k;
  return undefined;
}
function g(k: number): string | undefined {
  return k > 0 ? "s" + k : undefined;
}
function h(k: number): string | null | undefined {
  return k > 0 ? "h" : null;
}
console.log(f(1), f(0), g(2), g(0), h(0));
const v = f(0);
console.log(v, `${v} ${g(0)}`);
console.log(v === undefined, v ?? "dflt", f(3) ?? "dflt");
console.log(JSON.stringify({ a: f(0), b: 1 }));
