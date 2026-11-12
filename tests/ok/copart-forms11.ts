// read as TypeScript reads them: f().catch(() => ({ a })) where f gives { a, b } (read by the fields both have), an any[]
// where a data T[] goes, a union of objects inside an object literal written as JSON, structuredClone by what the value
// holds (each class its own copy, a Date a Date, Maps, Sets, Records; a function: DataCloneError), Object.assign(new C(), u)
// with u an unknown
// build: --tsc-checked
class Cadastro {
  falha = false;
  async cadastro(): Promise<{ porEmpresa: Map<string, number>; disponivel: boolean }> {
    if (this.falha) throw new Error('x');
    return { porEmpresa: new Map([['a', 1]]), disponivel: true };
  }
  async executa(): Promise<number> {
    const { porEmpresa } = await this.cadastro().catch(() => ({ porEmpresa: new Map<string, number>() }));
    return porEmpresa.size;
  }
}

function raw(): any[] {
  return JSON.parse('[{"pcParticipacao":10,"vlLimite":null},{"vlParticipacao":3}]');
}
let fatores: Array<{ pcParticipacao?: number | null; vlParticipacao?: number | null; vlLimite?: number | null }> = [];
fatores = raw();
console.log(fatores.length, fatores[0].pcParticipacao, fatores[1].vlParticipacao, fatores[1].pcParticipacao ?? 'nada');

interface DocA { id: string; elegibilidade: { n: number } }
interface DocG { id: string; global: boolean }
function indexa(o: object): void { console.log(JSON.stringify(o)); }
function salva(doc: DocA | DocG): void { indexa({ index: 'ix', document: doc, at: new Date(0) }); }
salva({ id: 'a', elegibilidade: { n: 1 } });
salva({ id: 'g', global: true });

class QueryResult { records: unknown[] = []; raw: unknown = null; }
interface Row { id: number; nome: string; quando: Date; tags: string[]; extra: Record<string, unknown> | null }
const r: Row = { id: 1, nome: 'a', quando: new Date(5000), tags: ['x', 'y'], extra: { k: [1, 2], d: new Date(7000) } };
const c = structuredClone(r);
c.tags.push('z');
c.quando.setTime(9000);
c.nome = 'b';
console.log(JSON.stringify(r), JSON.stringify(c), c.quando instanceof Date);
const rows: unknown = [r, new Map<string, number>([['a', 1]]), new Set<string>(['s'])];
const cl = structuredClone(rows) as unknown[];
console.log(cl.length, JSON.stringify(cl[0]), (cl[1] as Map<string, number>).get('a'), (cl[2] as Set<string>).has('s'));
try {
  structuredClone({ f: () => 1 } as unknown);
} catch {
  console.log('erro');
}
const tenta = (valor: unknown): unknown => {
  try {
    const clone = structuredClone(valor);
    return valor instanceof QueryResult ? Object.assign(new QueryResult(), clone) : clone;
  } catch {
    return null;
  }
};
const q = new QueryResult();
q.records = [{ a: 1 }, new Date(0)];
q.raw = 'x';
const qr = tenta(q);
console.log(qr instanceof QueryResult, JSON.stringify((qr as QueryResult).records), (qr as QueryResult).raw);
console.log(JSON.stringify(tenta({ n: 1, d: new Date(1000) })), tenta(() => 1));

async function main(): Promise<void> {
  const s = new Cadastro();
  console.log(await s.executa());
  s.falha = true;
  console.log(await s.executa());
}
main();
