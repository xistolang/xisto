// XB-219: a function passed through an any or unknown and read back as a function type (an int, number, boolean or
// string result; with or without captures; through `as` or an implicit conversion) is called with its own result
interface Span { name: string; end: () => void }
type Handler = (req: string, n: number) => Span;
function wrap(orig: any): Handler {
  const fn = orig as Handler;
  return (req: string, n: number): Span => {
    const s = fn(req, n);
    console.log("wrapped", s.name);
    return s;
  };
}
const h = wrap((req: string, n: number): Span => ({ name: req + n, end: (): void => { console.log("end"); } }));
h("a", 1).end();
const k = 3;
function asInt(cb: any): void { const f = cb as (v: int) => int; console.log(f(10)); }
asInt((v: int): int => v);
asInt((v: int): int => v * k);
function implicitInt(cb: any): void { const f: (v: int) => int = cb; console.log(f(10)); }
implicitInt((v: int): int => v + 1);
implicitInt((v: int): int => v + k);
function asNum(cb: unknown): void { const f = cb as (v: number) => number; console.log(f(10)); }
asNum((v: number): number => v + 0.5);
asNum((v: number): number => v + k);
function asStr(cb: unknown): void { const f = cb as (x: number) => string; console.log(f(4)); }
asStr((x: number) => `u${x}${k}`);
function asVoid(cb: any): void { const f = cb as (x: number) => void; f(5); }
asVoid((x: number) => { console.log("void", x + k); });
function asUnknown(cb: any): void { console.log((cb as (x: number) => unknown)(2)); }
asUnknown((x: number) => x * 10);
asUnknown((x: number) => x + k);
const fs: any[] = [(a: int): int => a + 1, (a: int): int => a * k];
for (const f of fs) { const g = f as (a: int) => int; console.log(g(7)); }
function mk(n: int): (a: int) => int { return (a: int): int => a + n; }
console.log((mk(5) as unknown as (a: int) => int)(2));
const pred: unknown = (s: string): boolean => s.length > k;
console.log((pred as (s: string) => boolean)("abcd"), (pred as (s: string) => boolean)("a"));
class Patch {
  orig: ((s: string) => string) | null = null;
  set(x: any): void { this.orig = x as (s: string) => string; }
  go(): string { return this.orig!("q"); }
}
const p = new Patch();
p.set((s: string) => s + s);
console.log(p.go());
