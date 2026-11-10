// 'k' in u, u a union of shapes: which member it holds, and if / else narrow it
type Valor = { sql: string } | { v: string | number | null; de?: string };
function expr(x: Valor): string {
  if ('sql' in x) return x.sql;
  return `${x.v}:${x.de ?? '-'}`;
}
type Res = { ok: number; dados: string[] } | { erro: string };
function show(r: Res): string {
  if ('erro' in r) return 'erro ' + r.erro;
  return `ok ${r.ok} ${r.dados.join(',')}`;
}
console.log(expr({ sql: 'SYSDATE' }), expr({ v: 3 }), expr({ v: 'a', de: 'N' }));
console.log(show({ erro: 'boom' }), show({ ok: 2, dados: ['a', 'b'] }));
