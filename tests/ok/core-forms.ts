// forms of the core app: an interface field named by a const ([KEY]?: T) and o[KEY], a class written as an expression,
// modules that import each other (entities), a subclass redeclaring a field of its base with a value
import { A } from './lib/cyc/a.ts';
import { B } from './lib/cyc/b.ts';
console.log(new A().filhos()[0].nome, new B().pai().nome);
const AUDIT_CONTEXT_KEY = '__audit' as const;
interface AuditContext { entity: string }
interface AuditableRequest {
  [AUDIT_CONTEXT_KEY]?: AuditContext;
  ip?: string;
}
const req: AuditableRequest = { ip: '1.2.3.4' };
req[AUDIT_CONTEXT_KEY] = { entity: 'perfil' };
const ctx = req[AUDIT_CONTEXT_KEY];
console.log(ctx?.entity, req.__audit?.entity, req.ip);
const NOOP = {
  getHandler: () => () => undefined,
  getClass: () => class {},
};
const k = NOOP.getClass();
console.log(typeof k, NOOP.getHandler()() === undefined);
class Logger { readonly ctx: string; constructor(ctx: string) { this.ctx = ctx; } log(m: string): string { return `[${this.ctx}] ${m}`; } }
class Base {
  protected readonly logger: Logger;
  constructor(nome: string) { this.logger = new Logger(nome); }
  diz(): string { return this.logger.log('base'); }
}
class Filho extends Base {
  protected readonly logger = new Logger('Filho');
  constructor() { super('Base'); }
  fala(): string { return this.logger.log('filho'); }
}
const f = new Filho();
console.log(f.diz(), f.fala());
