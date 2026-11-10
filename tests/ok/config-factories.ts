interface Cfg { enabled: boolean; nome: string; }
const liga = (n: string, padrao: boolean): boolean => n.length > 100 || padrao;
export const cfg = (): Cfg => ({ enabled: liga("X_NAO_EXISTE", true), nome: "x" });
export type Cfg2 = ReturnType<typeof cfg>;
export const NOMES = ["a", "b"] as const;
export type Nome = (typeof NOMES)[number];
type Parcial = { a?: number; b?: string };
const r: Required<Parcial> = { a: 1, b: "z" };
type Talvez = { v: number } | null;
const t: NonNullable<Talvez> = { v: 2 };
function usa(c: Cfg2, n: Nome): string { return `${c.enabled} ${c.nome} ${n} ${r.a} ${r.b} ${t.v}`; }
console.log(usa(cfg(), "b"));
