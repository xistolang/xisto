// Types TypeScript infers and people leave out: return types (functions, methods, block arrows), fields typed by
// their value, new Set/new Map without type arguments, object literals without a declared type, spreads, computed
// keys, and fields marked `!` (assigned before they are read).
interface Pessoa {
  nome: string;
  idade: number;
  email: string | null;
}

class Agenda {
  private static readonly JANELA = { ini: "22:00", fim: "06:00" };
  private static readonly TIPOS = new Set(["a", "b"]);
  private static readonly CODIGOS = new Set([10, 20]);
  private limites = { min: 1, max: 3 };
  private nomes = ["ana", "bia"];
  private porNome: Map<string, number> = new Map();
  private contagem = new Map([["x", 1], ["y", 2]]);
  private vistos: Set<string> = new Set();
  total!: number;

  constructor() {
    this.total = 0;
  }

  janela() { return `${Agenda.JANELA.ini}-${Agenda.JANELA.fim}`; }
  aceita(tipo: string, codigo: number) { return Agenda.TIPOS.has(tipo) && Agenda.CODIGOS.has(Math.trunc(codigo)); }
  add(nome: string) {
    this.vistos.add(nome);
    this.porNome.set(nome, (this.porNome.get(nome) ?? 0) + 1);
    this.contagem.set(nome, this.nomes.length);
    this.total += 1;
    return this;
  }
  faixa() { return { de: this.limites.min, ate: this.limites.max, n: this.nomes.length }; }
  faixaOu(x: number) { if (x > 0) return { de: x, ate: x * 2, n: 1 }; return this.faixa(); }
  primeiro() { if (this.vistos.size === 0) return null; return [...this.vistos][0]; }
  pares() { if (this.total === 0) return []; return [...this.porNome.values()].filter((v) => v % 2 === 0); }
  async conta() { return this.vistos.size + this.contagem.size; }
  log() { console.log(this.janela(), this.total); }
}

function dobro(n: number) { return n * 2; }
function tamanho(xs: string[]) { return xs.length; }
function nomeOu(s: string | null) { return s ?? "padrao"; }
function pessoa(nome: string) { return { nome, idade: 30, email: null as string | null }; }

async function main() {
  const a = new Agenda();
  console.log(a.janela(), a.aceita("a", 10), a.aceita("c", 20), a.primeiro(), a.pares().length);
  a.add("ana").add("bia").add("ana");
  const f = a.faixa();
  console.log(f.de, f.ate, f.n, a.faixaOu(2).ate, a.faixaOu(0).ate, a.primeiro(), a.pares().join(","), await a.conta());
  a.log();
  console.log(dobro(2.5), tamanho(["a", "b"]) + 0.5, nomeOu(null), nomeOu("x"));

  const fmt = (x: number) => { if (x > 2) return "grande"; return "pequeno"; };
  console.log([1, 3].map((x) => { const y = x * 10; return y + 1; }).join(","), fmt(3), fmt(1));

  const base: Pessoa = { nome: "ana", idade: 30, email: null };
  const comEmail: Pessoa = { ...base, email: "ana@x.org" };
  const maisVelha = { ...base, idade: base.idade + 1, ativo: true };
  const p = pessoa("bia");
  console.log(comEmail.nome, comEmail.email, maisVelha.idade, maisVelha.ativo, maisVelha.email, p.nome, p.email);
  console.log(JSON.stringify(f), JSON.stringify(comEmail), JSON.stringify(maisVelha));

  const campo = "total";
  const chave = `k${1}`;
  const porCampo = { [campo]: 10, [chave]: 20, fixo: 30 };
  const tipado: Record<string, string> = { [chave]: "v", outro: "w" };
  console.log(porCampo[campo], porCampo["k1"], porCampo["fixo"], tipado["k1"], tipado["outro"], Object.keys(porCampo).join(","));
  console.log(JSON.stringify(porCampo), JSON.stringify(tipado));
}
main();
