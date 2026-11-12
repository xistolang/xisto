// Symbol.for (one symbol per key), x instanceof Map / Set of an unknown, ArrayBuffer.isView, the ...rest of a Record
// build: --tsc-checked
const A = Symbol.for('tok');
const B = Symbol.for('tok');
const C = Symbol('tok');
console.log(A === B, A === C, String(A));
const m = new Map<string, number>();
const st = new Set<string>();
const rec: Record<string, number> = { a: 1 };
const xs: unknown[] = [m, st, rec, new Uint8Array(2), 'x'];
for (const x of xs) console.log(x instanceof Map, x instanceof Set, ArrayBuffer.isView(x));
function semFiltro(params: Record<string, unknown>): string[] {
  const { antesDeRun: _semFiltro, ...base } = params;
  return Object.keys(base);
}
console.log(semFiltro({ a: 1, antesDeRun: 'x', b: 2 }).join(','));
