// XB-173: union fields holding a function: typeof/instanceof on o.f and this.f, call after narrowing, assignment, JSON
class Fmt {
  prefix: string;
  constructor(p: string) { this.prefix = p; }
  run(n: number): string { return this.prefix + n; }
}
type H = Fmt | ((n: number) => string);
function use(h: H, n: number): string {
  if (h instanceof Fmt) return h.run(n);
  return h(n);
}
console.log(use(new Fmt("#"), 1), use((n: number) => `fn${n}`, 2));

interface Obs { next?: (v: number) => void }
function obj(o: Obs | ((v: number) => void)): string {
  if (typeof o === "object") return "object";
  return "function";
}
console.log(obj({}), obj((v: number) => {}));

let x: number | (() => number) = 5;
const show = (): string => (typeof x === "number" ? `num ${x + 1}` : `fn ${x()}`);
console.log(show());
x = () => 9;
console.log(show());

class Svc {
  handler: string | ((n: number) => string) | null = null;
  label(n: number): string {
    if (this.handler === null) return "none";
    if (typeof this.handler === "function") return this.handler(n);
    return this.handler;
  }
}
const s = new Svc();
console.log(s.label(1));
s.handler = "fixed";
console.log(s.label(2));
s.handler = (n: number) => `h${n * 10}`;
console.log(s.label(3));
console.log(s);
console.log(JSON.stringify(s));

interface Rule { message: string | ((p: string) => string) }
function render(r: Rule, p: string): string {
  if (typeof r.message === "function") {
    const m = r.message(p);
    r.message = "reset";
    return m + "/" + r.message;
  }
  return r.message;
}
const r: Rule = { message: (p) => `bad ${p}` };
console.log(render(r, "name"), render(r, "x"));

function pass(h: string | (() => string)): string { return typeof h === "string" ? h : h(); }
function relay(h: string | (() => string)): string { return pass(h); }
console.log(relay("a"), relay(() => "b"));

const hs: (string | ((n: number) => number))[] = [];
hs.push("s");
hs.push((n: number) => n * 2);
console.log(hs.map((h) => (typeof h === "function" ? h(21) : h.length)));
console.log(JSON.stringify(hs));
console.log(JSON.stringify({ list: hs, v: 1 }));

const opt: string | (() => string) | null = Math.random() > 2 ? "x" : null;
console.log(opt === null ? "null" : typeof opt);
