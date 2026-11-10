const ligado = (v: unknown): boolean => v === true || v === 'true';
console.log(ligado(true), ligado('true'), ligado(1), ligado(null));
const vazio = (v: unknown): boolean => v == null || v === '';
console.log(vazio(''), vazio('a'), vazio(null));
const n: number = 7.5;
const m = 3;
console.log(Math.min(m, n), Math.max(m, Math.trunc(n)));
class Q { async rows<T>(sql: string): Promise<T[]> { return []; } }
async function go(): Promise<void> { const r = await new Q().rows('x'); console.log(r.length); }
go();
