// ?. on void calls, await, arrays and Maps (get of a missing key through ?. and ??), short circuit, deep chains
class Logger { lines: string[] = []; log(s: string): void { this.lines.push(s); console.log("log:", s); } }
class Svc { logger: Logger | null = null; name: string | null = null; }
async function find(id: number): Promise<Svc | null> { if (id === 0) return null; const s = new Svc(); s.name = "svc" + String(id); return s; }
async function main(): Promise<void> {
  const s = new Svc();
  s.logger?.log("not printed");
  s.logger = new Logger();
  s.logger?.log("printed");
  console.log((await find(0))?.name ?? "none");
  console.log((await find(2))?.name ?? "none");
  const arr: number[] | null = [1, 2, 3];
  console.log(arr?.length, arr?.[1], arr?.map((x) => x * 2).join(","));
  const mm = new Map<string, number>(); mm.set("a", 1); const m: Map<string, number> | null = mm;
  console.log(m?.get("a") ?? 0, m?.get("b") ?? -1, m?.has("a"));
  const none: Map<string, number> | null = null;
  console.log(none?.size ?? 0);
  let count = 0;
  for (let i = 0; i < 5; i++) { const v: number | null = i % 2 === 0 ? Number(i) : null; count += v ?? 100; }
  console.log(count);
  const words = ["a", "bb", ""];
  for (const w of words) { const first: string | null = w.length > 0 ? w : null; console.log(first?.toUpperCase() ?? "(vazio)", first?.length); }
  const x: number = 5;
  console.log(x == 5, x != 4, "a" != "b", true == true);
}
const ab: boolean = true;
console.log(ab?.5:1, ab ? .25 : 2);
class Acc { total: number = 0; add(n: number): Acc { this.total += n; return this; } }
let hits = 0;
function bump(): number { hits++; return 1; }
const none: Acc | null = null;
const some: Acc | null = new Acc();
none?.add(bump());
some?.add(bump())?.add(bump());
console.log(hits, some?.total, none?.total);
interface Z { z: number | null; }
interface Y { y: Z | null; }
interface X { x: Y | null; }
const deep: X | null = { x: { y: { z: 0 } } };
console.log(deep?.x?.y?.z ?? 9, deep?.x?.y?.z == null);

main();
