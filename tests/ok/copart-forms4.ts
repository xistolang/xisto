// read as TypeScript reads them: a module constant a body reads before the module's code ran (a factory turned function,
// a method whose type a signature needs), typeof of a constant not checked yet, { ...a, ...b } of Records of different
// values, x ?? { k: v } where a union goes, (e ?? {}) as unknown as Record, a fresh int[] where number[] goes,
// String(v as string) of a union, an object literal and a Map<string, { same fields }> holding it (the same object)
// build: --tsc-checked
export class Dec {
  static readonly ZERO = new Dec(0);
  readonly v: number;
  constructor(v: number) {
    this.v = v;
  }
}
export const LIMITE = 3_400;
export const CHAVES = { escreve: 'E', worker: 'W' } as const;
export const ALTERAVEIS = [CHAVES.escreve, CHAVES.worker] as const;
export type Alteravel = (typeof ALTERAVEIS)[number];
export const SOMBRA = 'S';
export const cfg = () => {
  const teto = 34_000;
  return { lote: Math.floor(teto / LIMITE), zero: Dec.ZERO.v };
};
class Repo {
  atualiza(chave: Alteravel | typeof SOMBRA, v: string): string {
    return `${chave}=${v}`;
  }
  base() {
    return LIMITE * 2;
  }
}
interface Saude { ligado: true; desde: string }
class Svc {
  saude(): Saude {
    return { ligado: true, desde: 'ontem' };
  }
}
function ver(svc: Svc | null): Saude | { ligado: false } {
  return svc?.saude() ?? { ligado: false };
}
class Ent {
  cd: number = 1;
  nome: string = 'a';
}
function mapa(r: Record<string, any>): string {
  return Object.keys(r).join(',');
}
function chaves(m: Map<number, string>): number[] {
  return [...m].filter(([k]) => k > 1).map(([k]) => k);
}
class Cache {
  private readonly itens = new Map<string, { em: number; pronto: boolean }>();
  usa(k: string): boolean {
    const entrada = { em: Date.now(), pronto: false };
    this.itens.set(k, entrada);
    entrada.pronto = true;
    const got = this.itens.get(k);
    return got === entrada && got !== undefined && got.pronto;
  }
}
const r = new Repo();
console.log(JSON.stringify(cfg()), r.atualiza(SOMBRA, 'x'), r.atualiza('W', 'y'), r.base());
const binds: Record<string, number> = { a: 1, b: 2 };
const texto: Record<string, string> = { c: 'z' };
console.log(JSON.stringify({ ...binds, ...texto }));
const a = ver(new Svc());
const b = ver(null);
console.log(a.ligado, b.ligado, a.ligado ? a.desde : '-');
const e: Ent | null = null;
const e2: Ent | null = new Ent();
console.log(mapa((e2 ?? {}) as unknown as Record<string, any>), mapa((e ?? {}) as unknown as Record<string, any>) === '');
const ks = chaves(new Map<number, string>([[1, 'a'], [2, 'b'], [3, 'c']]));
console.log(ks.join(','), ks.map((x) => x / 2).join(','));
const linhas: { V: string | number | null }[] = [{ V: 'a' }, { V: 3 }, { V: null }];
console.log(linhas.map((l) => (l.V == null ? 'null' : String(l.V as string))).join(','));
console.log(new Cache().usa('k'));
