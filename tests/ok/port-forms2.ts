const CORES = ['azul', 'verde'] as const;
type Cor = (typeof CORES)[number];
const c: Cor = 'azul';
let res = { n: 1 };
res = { n: 2 } as typeof res;
const linhas: string[][] = [["a"]];
type Linha = (typeof linhas)[number];
const l: Linha = ["x", "y"];
console.log(c, res.n, l.length);
const linha = (campo: string, fmt?: string): string => campo + (fmt ?? "-");
console.log(linha("a"), linha("b", "x"));
const mes = (agora = new Date(2026, 0, 15)): string => String(agora.getMonth() + 1);
function ano(base = new Date(2025, 5, 1)): int { return base.getFullYear(); }
console.log(mes(), mes(new Date(2026, 4, 2)), ano());
class Acc { out: int[] = []; add(xs: int[]): void { this.out.push(...xs); } }
const a = new Acc();
a.add([1, 2]); a.add([3]);
const all: string[] = ["x"];
const more = (): string[] => ["y", "z"];
all.push(...more());
console.log(a.out.join(","), all.join(","));
const ru = { v: 1 } as { v: int } | undefined; console.log(ru?.v);
