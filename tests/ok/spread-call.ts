interface Ev { por: string; acao: string; quando: number }
interface EvC { por: string; acao: string; quando: number; confirmadoPor: string }
class S {
  n = 0;
  evento(por: string, acao: string): Ev { this.n++; return { por, acao, quando: this.n }; }
  conf(por: string): EvC { return { ...this.evento(por, "ENCERRAR"), confirmadoPor: "z" }; }
}
function contagem(): { a: number; b: number } { return { a: 0, b: 0 }; }
const s = new S();
const c = s.conf("isa");
const t = { ...contagem(), b: 5, c: "x" };
console.log(c.por, c.acao, c.quando, c.confirmadoPor, s.n, t.a, t.b, t.c);
