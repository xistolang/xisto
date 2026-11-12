// JSON.stringify of an unknown leaves a function out (a Record's) or writes null; a class with a Date field held as unknown
function f(v: unknown): string {
  if (Array.isArray(v)) return "arr " + JSON.stringify(v);
  const out: string[] = [];
  for (const [k, x] of Object.entries(v as Record<string, unknown>)) out.push(typeof x === "function" ? k + "=" + (x as () => string)() : k + ":" + JSON.stringify(x));
  return out.join(",") + " " + JSON.stringify(v);
}
console.log(f({ a: 1, dt: () => "SYSDATE", s: "x", d: new Date(0) }));
class E { a = 5; d = new Date(0); }
console.log(f(new E()));
const dto: { a: number; b?: string } = { a: 3 };
console.log(f(dto));
console.log(f([{ a: 1 }, { a: 2 }]));
