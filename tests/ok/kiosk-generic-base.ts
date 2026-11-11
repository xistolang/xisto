// forms of a NestJS port (kiosk): a class extending a generic class (class ProfileService extends BaseMongoService<Profile>),
// Partial<T> in a generic class (each instance copies T's fields, optional)
abstract class BaseRepo<T> {
  protected itens: T[] = [];
  protected abstract nome(): string;
  add(x: T): number {
    this.itens.push(x);
    return this.itens.length;
  }
  todos(): T[] { return this.itens; }
  descreve(): string { return this.nome() + ':' + this.itens.length; }
}
interface Perfil { id: string; nivel: number }
class PerfilRepo extends BaseRepo<Perfil> {
  protected nome(): string { return 'perfis'; }
  maiorNivel(): number {
    let m = 0;
    for (const p of this.itens) if (p.nivel > m) m = p.nivel;
    return m;
  }
}
class TextoRepo extends BaseRepo<string> {
  private readonly prefixo: string;
  constructor(prefixo: string) {
    super();
    this.prefixo = prefixo;
  }
  protected nome(): string { return this.prefixo; }
}
const r = new PerfilRepo();
r.add({ id: 'a', nivel: 3 });
r.add({ id: 'b', nivel: 7 });
const t = new TextoRepo('txt');
t.add('x');
const base: BaseRepo<Perfil> = r;
console.log(r.maiorNivel(), r.descreve(), t.descreve(), JSON.stringify(t.todos()), base.descreve(), base.todos().length);
class Repo2<T> {
  itens: T[] = [];
  constructor(itens: T[]) { this.itens = itens; }
  atualiza(i: int, parcial: Partial<T>): string {
    return JSON.stringify(this.itens[i]) + ' <- ' + JSON.stringify(parcial);
  }
}
interface Conta { nome: string; nivel: number }
const r2 = new Repo2<Conta>([{ nome: 'a', nivel: 1 }]);
console.log(r2.atualiza(0, { nivel: 2 }));
