interface Base { id: int; nome: string; extra: string }
interface Mais extends Base { idade: int }
type ComDet = Base & { detalhe: string | null };
type SemExtra = Omit<Base, 'extra' | 'nome'>;
type SoNome = Pick<Mais, 'nome' | 'idade'>;
type Parc = Partial<Base>;
type Status = 'A' | 'B' | null;
function f(b: Base): ComDet { return { id: b.id, nome: b.nome, extra: b.extra, detalhe: "d" }; }
const isStr = (s: string | null): s is string => s !== null;
const b: Base = { id: 1, nome: "x", extra: "e" };
const c = f(b);
console.log(c.id, c.nome, c.detalhe);
const s: SemExtra = { id: 2 };
console.log(s.id);
const k: SoNome = { nome: "n", idade: 3 };
console.log(k.nome, k.idade);
const p: Parc = { id: 5 };
console.log(p.id, p.nome ?? "none");
let st: Status = null;
console.log(st);
st = 'A';
console.log(st);
const xs: (string | null)[] = ["a", null, "b"];
console.log(xs.filter((g): g is string => g !== null).join(","), isStr("q"));
const taxas: int[] = [];
const taxa = taxas.length > 0 ? (taxas[0] ?? null) : null;
console.log(taxa);
const arr: Array<Base & { tag: string }> = [{ id: 9, nome: "n", extra: "", tag: "t" }];
console.log(arr[0].tag, arr[0].id);
interface Doc { '@timestamp': string; 'event.kind': string; nome: string }
const doc: Doc = { '@timestamp': "2026-01-01", 'event.kind': "x", nome: "n" };
console.log(doc['@timestamp'], doc['event.kind'], doc.nome);
console.log(JSON.stringify(doc));
const chave = 'CHAVE' satisfies string;
console.log(chave);
type Contagem = Partial<Record<'A' | 'B', number>>;
const cont: Contagem = { A: 2 };
console.log(cont['A'] ?? 0, cont['B'] ?? 0);
const porStatus: Record<number, string> = { 0: "zero", 3: "tres" };
console.log(porStatus[3], porStatus[0]);
type Resultado = { ok: string } | { erro: string };
function avalia(r: Resultado): string { return typeof r; }
console.log(avalia({ ok: "x" }));
class Timer { n: int = 0; unref(): void { this.n++; } }
const tm = new Timer();
tm.unref?.();
console.log(tm.n);
