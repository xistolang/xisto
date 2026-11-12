// forms the Mongo front met: a function with fewer parameters where a callback goes, Date.now as a value, Pick<C, 'm'> of a
// class's methods, Object.assign(this, partial) and a literal of a class whose constructor takes its own type,
// Object.keys/entries/values of objects with optional fields, a union's array cast and passed as unknown[], map to Provider[]
class Emissor {
  private handlers: ((a: unknown) => void)[] = [];
  once(ev: string, fn: (a: unknown) => void): void { this.handlers.push(fn); }
  emit(v: unknown): void { for (const h of this.handlers) h(v); }
}
const logs: string[] = [];
function quieto(): void { logs.push('quieto'); }
function attach(e: Emissor, nome: string): void {
  const report = (): void => { logs.push('conectado ' + nome); };
  e.once('connected', report);
  e.once('connected', quieto);
}
const em = new Emissor();
attach(em, 'db');
em.emit(1);
console.log(logs.join(', '));
interface Campo { tipo: string; padrao: () => number }
const c: Campo = { tipo: 'Date', padrao: Date.now };
const agora = Date.now();
const v = c.padrao();
console.log(c.tipo, v >= agora && v - agora < 5000);
class Registro {
  linhas: string[] = [];
  log(m: string): void { this.linhas.push('LOG ' + m); }
  warn(m: string): void { this.linhas.push('WARN ' + m); }
  error(m: string): void { this.linhas.push('ERR ' + m); }
  debug(m: string): void { this.linhas.push('DBG ' + m); }
}
type Sink = Pick<Registro, 'log' | 'warn' | 'error'>;
function relata(s: Sink, ok: boolean): void {
  s.log('inicio');
  if (!ok) s.warn('falhou');
}
const r = new Registro();
relata(r, false);
console.log(r.linhas.join(' | '));
class PageDto<T> {
  items: T[];
  total: number;
  page: number;
  constructor(partial: PageDto<T>) {
    Object.assign(this, partial);
  }
}
function pagina<T>(xs: T[], page: number): PageDto<T> {
  return new PageDto<T>({ items: xs, total: xs.length, page });
}
const p = pagina(['a', 'b'], 1);
console.log(JSON.stringify(p), p.items.length);
interface Settings { zoom: number; idioma?: string; tema?: string }
class Atualiza { nome?: string; ativo: boolean = true; nota: number = 0; }
const s: Settings = { zoom: 2, idioma: 'pt' };
console.log(Object.keys(s).join(','), JSON.stringify(Object.entries(s)));
const a = new Atualiza();
a.nome = 'x';
for (const [k, v] of Object.entries(a)) console.log(k, v);
const nova = new Atualiza();
console.log(Object.values(nova).length);
class Svc { nome = 'svc'; }
interface ValueP { provide: string; useValue: number }
interface FactoryP { provide: string; useFactory: () => number; inject?: unknown[] }
type Prov = Svc | ValueP | FactoryP;
function registra(inject: unknown[] | null, ps: Prov[]): string {
  return `${inject === null ? 0 : inject.length} ${ps.length}`;
}
const nomes = ['a', 'bb'];
const provs: Prov[] = nomes.map((n) => ({ provide: n, useFactory: (): number => n.length }));
const deps: (Svc | string)[] | null = [new Svc(), 'TOKEN'];
const como = deps as Prov[] | null;
console.log(registra(como, provs));
