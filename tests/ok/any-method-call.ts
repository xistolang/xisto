// methods called on an any: looked up by name among the classes that have them, a string's and a number's own; x.m?.() undefined when m is not there; a TypeError when nothing has it
class Span { tags: string[] = []; setTag(k: string, v: unknown): void { this.tags.push(k + "=" + String(v)); } finish(): void { this.tags.push("done"); } }
function apm(span: any): string {
  span?.setTag?.("a", 1);
  span?.finish?.();
  span?.missing?.(3);
  return span ? (span as Span).tags.join(",") : "none";
}
class Runner {
  n = 0;
  async connect(): Promise<void> { this.n++; }
  async query(sql: string): Promise<number> { return sql.length + this.n; }
  name(prefix: string): string { return prefix + "runner"; }
}
class Other { name(p: string): string { return p + "other"; } }
async function main(): Promise<void> {
  const qr: any = new Runner();
  await qr.connect();
  console.log(await qr.query("abc"), qr.name(">"), (new Other() as any).name("<"));
  const s: any = "abc";
  console.log(s.toUpperCase(), (3.14159 as any).toFixed(2));
  try { qr.nope(); } catch (e) { console.log((e as Error).message); }
  const z: any = null;
  try { z.name("x"); } catch (e) { console.log((e as Error).message); }
  console.log(z?.name("x") ?? "undef");
  console.log(apm(new Span()), apm(null));
}
main();
