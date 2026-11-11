// forms of a NestJS port (kiosk): object rest in destructuring, Types.ObjectId (a type through an imported name),
// Parameters<typeof f>[i], optional interface methods m?(x): R, generic function types <T>(x) => R and generic
// arrows inside functions (held with T as unknown; f<Row>(x) converts the result to Row)
import { Types } from './lib/kiosk/ids.ts';
import * as Ids from './lib/kiosk/ids.ts';
class Rollout { grupoId: Types.ObjectId | null = null; outro: Ids.ObjectId | null = null; }
console.log(new Rollout().grupoId === null, Types.marca);
class AgendarDto { cdEspecialidade: number = 0; dtAgenda: string = ''; obs?: string; }
type Corpo = AgendarDto & { nuUsuario: number; cdUsuario: string; cdEmpresaPlano: number };
function agendar(dto: AgendarDto, nu: number, cd: string, emp: number): string {
  return JSON.stringify(dto) + ` ${nu} ${cd} ${emp}`;
}
function rota(body: Corpo): string {
  const { nuUsuario, cdUsuario, cdEmpresaPlano, ...dto } = body;
  return agendar(dto, nuUsuario, cdUsuario, cdEmpresaPlano);
}
interface Ponto { x: number; y: number; z: number; nome: string }
const semZ = ({ z, ...resto }: Ponto): string => `${z} ${JSON.stringify(resto)}`;
function semNome({ nome, ...xyz }: Ponto): number { return xyz.x + xyz.y + xyz.z + nome.length; }
const p: Ponto = { x: 1, y: 2, z: 3, nome: 'pt' };
const b: Corpo = { cdEspecialidade: 7, dtAgenda: '2026-11-12', nuUsuario: 10, cdUsuario: 'U1', cdEmpresaPlano: 1 };
console.log(rota(b));
console.log(semZ(p), semNome(p));
const { x, ...outros } = p;
console.log(x, outros.y, outros.nome, JSON.stringify(outros));
interface Ramo { cd: number; inadimplente: boolean }
function avaliar(ramos: Ramo[], limite: number): number { return ramos.filter((r) => r.inadimplente).length + limite; }
function montar(dsRamos: Parameters<typeof avaliar>[0], lim: Parameters<typeof avaliar>[1]): number {
  return avaliar(dsRamos, lim);
}
console.log(montar([{ cd: 1, inadimplente: true }, { cd: 2, inadimplente: false }], 10));
type QueryOne = <T>(sql: string, binds: unknown[]) => Promise<T | null | undefined>;
interface Row { CD_USUARIO: string; N: number }
class Base {
  rows: Row[] = [{ CD_USUARIO: 'abc', N: 3 }];
  protected async executeQueryOne<T>(sql: string, params: unknown[] = []): Promise<T | null> {
    if (sql === 'vazio') return null;
    return (this.rows[0] as unknown as T) ?? null;
  }
  async rodar(): Promise<string> {
    const r1 = await resolver((sql, binds) => this.executeQueryOne(sql, binds), 'sql');
    const r2 = await resolver((sql, binds) => this.executeQueryOne(sql, binds), 'vazio');
    const queryOne = async <T>(sql: string, binds: unknown[]): Promise<T | null> => {
      const r = await this.executeQueryOne<T>(sql, binds);
      return r ?? null;
    };
    const row = await queryOne<Row>('x', [1]);
    const first = <T>(xs: T[]): T | null => (xs.length ? xs[0] : null);
    const n = first<number>([4, 5]);
    return `${r1} ${r2} ${row?.CD_USUARIO} ${row?.N} ${n}`;
  }
}
async function resolver(queryOne: QueryOne, sql: string): Promise<string | null> {
  const row = await queryOne<Row>(sql, [1]);
  if (row?.CD_USUARIO != null) return String(row.CD_USUARIO).toUpperCase();
  return null;
}
interface Deps {
  propriedade(nome: string): Promise<string | null>;
  operadoraUsuario?(cdUsuario: string): Promise<number | null>;
  formata?(n: number, casas: number): string;
}
async function atualiza(deps: Deps, cd: string): Promise<string> {
  const p = await deps.propriedade('ATUALIZA');
  const op = (await deps.operadoraUsuario?.(cd)) ?? -1;
  const f = deps.formata?.(3.14159, 2) ?? 'sem formata';
  return `${p} ${op} ${f}`;
}
class Completo implements Deps {
  async propriedade(nome: string): Promise<string | null> { return nome.toLowerCase(); }
  operadoraUsuario = async (cd: string): Promise<number | null> => cd.length;
  formata = (n: number, casas: number): string => n.toFixed(2) + '/' + casas;
}
class Minimo implements Deps {
  async propriedade(nome: string): Promise<string | null> { return null; }
}
async function main(): Promise<void> {
  console.log(await new Base().rodar());
  console.log(await atualiza(new Completo(), 'abc'));
  console.log(await atualiza(new Minimo(), 'abc'));
}
main();
