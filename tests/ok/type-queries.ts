// Types read from other declarations: T['field'] (indexed access), ReturnType<typeof f>, and a field typed by the
// module const it starts with.
interface Pedido {
  id: number;
  cliente: string;
  itens: string[];
  desconto: number;
}

function carrega(id: number) {
  return { id, cliente: "ana", itens: ["a", "b"], desconto: 0 };
}

function resumo(c: Pedido["cliente"], itens: Pedido["itens"], d: Pedido["desconto"]): string {
  return `${c}:${itens.length}:${d}`;
}

const MODO = "rapido";
const LIMITE = 3;

class Tela {
  readonly modo = MODO;
  readonly limite = LIMITE;
  private ultimo: ReturnType<typeof carrega> | null = null;
  abre(id: number): void { this.ultimo = carrega(id); }
  texto(): string {
    const u = this.ultimo;
    return u === null ? "-" : resumo(u.cliente, u.itens, u.desconto);
  }
}

const t = new Tela();
console.log(t.texto());
t.abre(7);
console.log(t.texto(), resumo("bia", [], 5), t.modo, t.limite / 2);
