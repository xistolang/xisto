// appa round 6: structural copies into data classes and arrays, aliases of intersections, generic callbacks,
// a soft cast before ?., ?? with an object default, xs.length = n

class RespDto {
  a: string = "";
  n: number = 0;
}
interface Row {
  a: string;
  n: number;
}
interface RowX {
  a: string;
  n: number;
  extra: boolean;
}
function f1(): RespDto {
  return { a: "x", n: 1 };
}
function f2(r: Row): RespDto {
  return r;
}
function total(xs: Row[]): number {
  let t = 0;
  for (const x of xs) t += x.n;
  return t;
}
const ys: RowX[] = [
  { a: "q", n: 2, extra: true },
  { a: "r", n: 5, extra: false },
];
console.log(f1().a, f2({ a: "y", n: 3 }).n, total(ys));

interface Ext {
  obs?: string | null;
}
type Ped = { status: number; ver: string } & Ext;
type Fila = Ped & { detalhe?: string | null };
function sem(base: { status: number; ver: string }, d: string): Fila & { detalhe: string } {
  return { ...base, detalhe: d };
}
function fecha(r: Fila): string {
  return `${r.status} ${r.ver} ${r.detalhe ?? "-"} ${r.obs ?? "-"}`;
}
console.log(fecha(sem({ status: 5, ver: "D" }, "abc")));

class Conn {
  n = 7;
}
async function usa(c: Conn): Promise<number> {
  return c.n;
}
class Pool {
  async com<T>(fn: (c: Conn) => Promise<T>): Promise<T> {
    return await fn(new Conn());
  }
}
function msg(err: unknown): string {
  return String((err as Error)?.message ?? err).split("\n")[0];
}
async function main(): Promise<void> {
  const v = await new Pool().com(async (c) => await usa(c));
  console.log("generic callback", v);

  console.log(msg(new Error("boom\nstack")), msg("plain"), msg(42));

  const xs = [1, 2, 3, 4, 5];
  xs.length = 2;
  console.log(xs.length, xs.join(","));
  const names: string[] = ["a", "b"];
  names.length = 0;
  names.push("c");
  console.log(names);
}
main().then(() => rest());

// filter with a type predicate, new Map(xs.map(...)) of pairs, ?: of T and Promise<T>, Buffer.byteLength(s, enc)
function rest(): void {
  const maybe: (string | null)[] = ["a", null, "b", "a"];
  const only = maybe.filter((x): x is string => x !== null);
  console.log([...new Set(only)].join("+"));
  const pairs = new Map(only.map((s, i) => [s, i]));
  console.log(pairs.get("a"), pairs.get("b"), pairs.size);
  console.log(Buffer.byteLength("ação", "utf8"), Buffer.byteLength("ação", "latin1"));
  later();
}
async function load(): Promise<number> {
  return 9;
}
async function pick(cached: number | null): Promise<number> {
  return await (cached !== null ? cached : load());
}
function later(): void {
  pick(4).then((a) => pick(null).then((b) => console.log("pick", a, b)));
}
