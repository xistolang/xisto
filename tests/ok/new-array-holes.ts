// new Array<R>(n) of objects or strings filled by index later: its holes are null until set
async function mapComLimite<T, R>(itens: T[], limite: number, fn: (item: T, indice: number) => Promise<R>): Promise<R[]> {
  const resultado = new Array<R>(itens.length);
  if (itens.length === 0) return resultado;
  let proximo = 0;
  const trabalhador = async (): Promise<void> => {
    while (proximo < itens.length) {
      const indice = proximo;
      proximo += 1;
      resultado[indice] = await fn(itens[indice], indice);
    }
  };
  const ws: Promise<void>[] = [];
  for (let i = 0; i < Math.min(limite, itens.length); i++) ws.push(trabalhador());
  await Promise.all(ws);
  return resultado;
}
interface P { id: number; nome: string }
async function main(): Promise<void> {
  const r = await mapComLimite([1, 2, 3], 2, async (n: number): Promise<P> => ({ id: n, nome: "n" + n }));
  console.log(JSON.stringify(r));
  const s = await mapComLimite(["a", "b"], 5, async (x: string): Promise<string> => x.toUpperCase());
  console.log(s.join(","), (await mapComLimite([] as string[], 1, async (x: string): Promise<string> => x)).length);
}
main();
