// a variable a closure reassigns (held in a cell), narrowed to not null by an assignment or a test, as TypeScript
// narrows it: read as narrowed (each read checked), tested against null as declared, seen as declared in a callback
function addMonths(t: string | null, n: number): string | null {
  return t === null ? null : t + "/" + n;
}
function dataDe(a: string): string {
  return a + "-01";
}
class Item {
  nome = "item";
}
function refund(vdtSol: string, ciclos: number): string {
  let ldtBase: string | null = null;
  let qtd: number | null = null;
  let item: Item | null = null;
  const inicializa = (): void => {
    ldtBase = null;
    qtd = null;
    item = null;
  };
  const saida: string[] = [];
  for (let i = 0; i < ciclos; i++) {
    inicializa();
    ldtBase = dataDe(i % 2 === 0 ? "2025" : "2023");
    if (ldtBase > vdtSol) ldtBase = addMonths(ldtBase, -12);
    saida.push(String(ldtBase));
    qtd = i * 2;
    qtd = qtd + 1;
    qtd = qtd * 1;
    saida.push(String(qtd + 0.5), qtd.toFixed(1));
    item = new Item();
    item.nome = item.nome + i;
    saida.push(item.nome);
    if (ldtBase !== null) {
      const tam = ldtBase.length;
      saida.push(ldtBase.slice(0, 4) + ":" + tam);
    }
    const ler = (): string => (ldtBase === null ? "nulo" : ldtBase.toUpperCase());
    saida.push(ler());
    inicializa();
    saida.push(String(ldtBase === null), String(ldtBase ?? "vazio"), String(qtd === null), String(item === null), ler());
  }
  return saida.join(" ");
}
console.log(refund("2024-06-01", 3));
