export type Item<T> = { ok: true; valor: T } | { ok: false; erro: unknown };
function mk(n: number): Item<number> { return n > 0 ? { ok: true, valor: n } : { ok: false, erro: new Error("neg") }; }
function pega(item: Item<number>): number { if (!item.ok) throw item.erro; return item.valor; }
const valorOu = (item: Item<string>, padrao: string): string => (item.ok ? item.valor : padrao);
const s: Item<string> = { ok: false, erro: null };
console.log(pega(mk(3)), valorOu(s, "p"));
try { pega(mk(-1)); } catch (e) { console.log((e as Error).message); }
