// a function field with an optional parameter called without it, and Array.isArray(x) ? x : [x] over two shapes
function ord(column: string) {
  const unchecked: number | undefined = column === "A" ? 1 : undefined;
  return {
    column,
    expression: (alias?: string) => {
      const ref = alias ? `${alias}.${column}` : column;
      return unchecked === undefined ? ref : `NVL(${ref}, ${unchecked})`;
    },
  };
}
const o = ord("A");
console.log(o.expression(), o.expression("t"));
function lst(exames: Array<{ codigoAtendimento: number }> | { codigoAtendimento: number; senha?: string }): number {
  const lista = Array.isArray(exames) ? exames : [exames];
  let s = 0;
  for (const e of lista) s += e.codigoAtendimento;
  return s;
}
console.log(lst([{ codigoAtendimento: 1 }, { codigoAtendimento: 2 }]), lst({ codigoAtendimento: 5 }));
function senhas(x: Array<{ id: number; senha?: string }> | { id: number }): string {
  const l = Array.isArray(x) ? x : [x];
  return JSON.stringify(l);
}
console.log(senhas([{ id: 1, senha: "s" }, { id: 2 }]), senhas({ id: 3 }));
