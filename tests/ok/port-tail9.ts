// build: --tsc-checked
// warns: XS2106
// warns: XS2107
class Dec {
  private readonly escala: int;
  constructor(e: int) { this.escala = e; }
  static max(a: Dec, b: Dec): int { return Math.max(a.escala, b.escala); }
}
console.log(Dec.max(new Dec(2), new Dec(5)));
console.log(Number(true), Number(false), [true, false, true].map((b) => Number(b)));
const q = (sql: string, binds: Record<string, string> = {}): string => sql + ":" + Object.keys(binds).length;
console.log(q("a"), q("b", { x: "1" }));
const buf: string[] = ["a", "b", "c", "d"];
const rest = buf.splice(1);
console.log(buf, rest);
const all = buf.splice(0);
console.log(buf, all);
try { throw new Error("outer", { cause: new Error("inner") }); } catch (e) { console.log((e as Error).message); }
class C {
  private readonly cb: ((n: int) => void) | null;
  constructor(cb: ((n: int) => void) | null) { this.cb = cb; }
  fire(n: int): void { if (this.cb) { this.cb(n); } }
}
new C((n: int) => console.log("cb", n)).fire(3);
new C(null).fire(4);
const v: unknown = Buffer.from("hi");
const w: unknown = "x";
console.log(Buffer.isBuffer(v), Buffer.isBuffer(w));
const parsed = JSON.parse('{"version":"1.2","n":3}');
console.log(parsed.version ?? "unknown", parsed.missing ?? "dflt");
console.log("end");
const bs: boolean[] = [true, false];
console.log(bs.map((b) => (b ? 1 : 0)), bs.map((b) => !b), bs.map((b) => (b ? "y" : "n")), [1, 2].map((n) => n > 1), ["a"].map((s) => s === "a"));
