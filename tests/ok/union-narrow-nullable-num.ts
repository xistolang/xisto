// a union narrowed by typeof to number | null: read unboxed (?? and arithmetic) and boxed again (unknown, JSON)
function pick(n: number): number | null | string {
  if (n === 0) return "erro";
  if (n < 0) return null;
  return n * 10;
}
function f(n: number): string {
  const t = pick(n);
  if (typeof t === "string") return t;
  const a = t ?? 0;
  const u: unknown = t;
  return `${a} ${JSON.stringify(u)}`;
}
console.log(f(0), f(-1), f(2));
