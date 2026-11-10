export type Item<T> = { ok: true; valor: T } | { ok: false; erro: unknown };
async function lanca<T>(fn: () => Promise<T>): Promise<Item<T>> {
  try { const v = await fn(); return { ok: true, valor: v }; } catch (e) { return { ok: false, erro: e }; }
}
function pega<T>(item: Item<T>): T { if (!item.ok) throw item.erro; return item.valor; }
const valorOu = <T>(item: Item<T>, padrao: T): T => (item.ok ? item.valor : padrao);
async function n(): Promise<number> { return 4; }
async function bad(): Promise<string> { throw new Error("no"); }
async function main(): Promise<void> {
  const a = await lanca(n);
  const b = await lanca(bad);
  console.log(pega(a), valorOu(b, "padrao"));
  try { pega(b); } catch (e) { console.log((e as Error).message); }
}
main();
