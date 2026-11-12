// variance (Isa, 12/11): a Record<string, number> where a Record<string, unknown> goes, copied when the callee only reads
// that parameter (here through a function that only reads it too); a function of (g: Guia) where one of (x: unknown)
// goes, adapted when the callee only calls it
// build: --tsc-checked
function lista(binds: Record<string, unknown>): string {
  return Object.keys(binds).map((k) => `${k}=${String(binds[k])}`).join('&');
}
function soma(binds: Record<string, unknown>): string {
  return lista(binds) + (binds['a'] !== undefined ? '!' : '');
}
interface Guia { id: number; nome: string }
function agrupa(xs: unknown[], k: (x: unknown) => string): string[] {
  return xs.map((x) => k(x));
}
const chave = (g: Guia): string => `${g.id}-${g.nome}`;
const nums: Record<string, number> = { a: 1, b: 2 };
console.log(soma(nums));
const gs: Guia[] = [{ id: 1, nome: 'a' }, { id: 2, nome: 'b' }];
console.log(agrupa(gs, chave).join(','));
