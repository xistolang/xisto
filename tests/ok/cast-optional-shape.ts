// (x as { k?: T })?.k over an object of a class: its own k, or undefined when it has none; [a, b] of two classes; const x = null; filter((d): d is NonNullable<typeof d> => !!d)
class Ecg {
  private readonly _code: string;
  private constructor(code: string) { this._code = code; }
  code(): string { return this._code; }
  static readonly OK = new Ecg("OK");
  static of(c: string): Ecg | null { return c === "" ? null : new Ecg(c); }
}
function ok(r: Ecg | null): boolean {
  return r === Ecg.OK || (r !== null && (r as { code?: () => string }).code?.() === "OK");
}
console.log(ok(Ecg.OK), ok(Ecg.of("OK")), ok(Ecg.of("E1")), ok(null));
function kind(e: unknown): string {
  try { throw e; } catch (err) {
    const k = (err as { utlHttpError?: string }).utlHttpError;
    const c = (err as { cause?: unknown }).cause;
    return `${k ?? "none"} ${c == null ? "nocause" : String(c)}`;
  }
}
console.log(kind(new Error("x")), kind(new Error("y", { cause: "root" })));
class A { a = 1; }
class B { b = 2; }
function both(xs: unknown[]): number { return xs.length; }
const alvos = [new A(), new B()];
console.log(both(alvos));
const cdFil = null;
function fer(o: { dias: number; cdFil: string | null }): string { return `${o.dias} ${o.cdFil}`; }
console.log(fer({ dias: 2, cdFil }));
interface R { dedo?: number | null; nome?: string | null }
const rs: (R | null)[] = [{ dedo: 1, nome: "a" }, null, { dedo: null, nome: null }, { dedo: 3, nome: "c" }];
const nomes = rs.map((r) => r?.nome).filter((d): d is NonNullable<typeof d> => !!d).map((d) => d.toUpperCase());
const dedos = rs.map((r) => r?.dedo).filter((d): d is NonNullable<typeof d> => !!d).map((d) => d * 10);
console.log(nomes.join(","), dedos.join(","));
function kind2(e: unknown): string {
  try { throw e; } catch (err) {
    const k = (err as { utlHttpError?: string } | null)?.utlHttpError;
    const c = (err as { cause?: unknown } | null)?.cause;
    return `${k ?? "none"} ${c == null ? "nocause" : String(c)}`;
  }
}
console.log(kind2(new Error("x")), kind2(new Error("y", { cause: "root" })));
function num(e: Error | null): number {
  const n = (e as { errorNum?: number } | null)?.errorNum;
  return n ?? -1;
}
console.log(num(new Error("a")), num(null));
