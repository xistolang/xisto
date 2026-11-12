// ondas do pedido (docs/proposta-ondas.md, option A): emOndaSettled / emOnda / valorDaOnda over mapped tuples, a synthetic
// copy of the app's file: T erased inside (closures giving unknown), each element of the result converted at the call
// (checked); heterogeneous tuples (a class object, number | null, string), a sync throw, a rejection, emOnda's rethrow in
// declared order, identity (===) of an object an item gives, an array of closures (boolean[]), destructuring
// build: --tsc-checked
export type ItemDaOnda<T> = { ok: true; valor: T } | { ok: false; erro: unknown };

export async function emOndaSettled<T extends readonly unknown[]>(fns: {
  [K in keyof T]: () => Promise<T[K]>;
}): Promise<{ [K in keyof T]: ItemDaOnda<T[K]> }> {
  const settled = await Promise.allSettled(
    (fns as readonly (() => Promise<unknown>)[]).map((f) => {
      try {
        return f();
      } catch (e) {
        return Promise.reject(e);
      }
    }),
  );
  return settled.map((s) =>
    s.status === 'fulfilled' ? { ok: true, valor: s.value } : { ok: false, erro: s.reason },
  ) as { [K in keyof T]: ItemDaOnda<T[K]> };
}

export async function emOnda<T extends readonly unknown[]>(fns: {
  [K in keyof T]: () => Promise<T[K]>;
}): Promise<T> {
  const itens = await emOndaSettled<T>(fns);
  for (const item of itens as readonly ItemDaOnda<unknown>[]) {
    if (!item.ok) throw item.erro;
  }
  return (itens as readonly ItemDaOnda<unknown>[]).map(
    (i) => (i as { valor: unknown }).valor,
  ) as unknown as T;
}

export function valorDaOnda<T>(item: ItemDaOnda<T>): T {
  if (!item.ok) throw item.erro;
  return item.valor;
}

class Valoracao {
  valor: number;
  tabela: string;
  constructor(valor: number, tabela: string) {
    this.valor = valor;
    this.tabela = tabela;
  }
}
const ordem: string[] = [];
const VAL = new Valoracao(12.5, 'AMB');
async function leFator(n: number): Promise<number | null> {
  ordem.push('fator');
  return n > 0 ? n * 2 : null;
}
async function leRede(): Promise<string> {
  ordem.push('rede');
  return 'NP';
}
async function leValor(): Promise<Valoracao> {
  ordem.push('valor');
  return VAL;
}
async function falha(msg: string): Promise<number> {
  ordem.push('falha ' + msg);
  throw new Error(msg);
}
function lancaSincrono(): Promise<string> {
  throw new Error('sincrono');
}
async function ok(b: boolean): Promise<boolean> {
  return b;
}

// two instances of one union: ItemDaOnda<T> where ItemDaOnda<T | null> goes (a new item, the value the same)
async function lanca<T>(fn: () => Promise<T>): Promise<ItemDaOnda<T>> {
  try {
    return { ok: true, valor: await fn() };
  } catch (erro) {
    return { ok: false, erro };
  }
}
function lancaSe<T>(porta: boolean, fn: () => Promise<T>): Promise<ItemDaOnda<T | null>> {
  return porta ? lanca(fn) : Promise.resolve({ ok: true, valor: null });
}
type Leitura<T> = { tipo: 'linha'; linha: T } | { tipo: 'sem-linha' } | { tipo: 'erro'; sqlerrm: string };
function selectInto<T>(linhas: readonly T[]): { tipo: 'linha'; linha: T } | { tipo: 'sem-linha' } | { tipo: 'erro'; sqlerrm: string } {
  if (linhas.length === 1) return { tipo: 'linha', linha: linhas[0] };
  if (linhas.length === 0) return { tipo: 'sem-linha' };
  return { tipo: 'erro', sqlerrm: 'ORA-01422' };
}
function controle(xs: Valoracao[]): Leitura<Valoracao> {
  return selectInto(xs);
}

async function main(): Promise<void> {
  const l1 = await lancaSe(true, async () => VAL);
  const l2 = await lancaSe(false, async () => 5);
  console.log(l1.ok ? l1.valor === VAL : false, l2.ok ? l2.valor : -1);
  const c1 = controle([VAL]);
  const c2 = controle([]);
  const c3 = controle([VAL, VAL]);
  console.log(c1.tipo === 'linha' ? c1.linha === VAL : false, c2.tipo, c3.tipo === 'erro' ? c3.sqlerrm : '');
  const onda3 = await emOndaSettled<[number | null, string, Valoracao]>([() => leFator(3), () => leRede(), () => leValor()]);
  if (!onda3[0].ok) throw onda3[0].erro;
  const fator = onda3[0].valor;
  console.log(fator, valorDaOnda(onda3[1]).length, valorDaOnda(onda3[2]) === VAL, valorDaOnda(onda3[2]).tabela);
  const valorOu = <T>(item: ItemDaOnda<T>, padrao: T): T => (item.ok ? item.valor : padrao);
  const onda2 = await emOndaSettled<[number | null, number, string]>([() => leFator(0), () => falha('a'), () => lancaSincrono()]);
  console.log(valorOu(onda2[0], -1), valorOu(onda2[1], 7), valorOu(onda2[2], 'padrao'));
  console.log(onda2[1].ok ? 'ok' : (onda2[1].erro as Error).message, onda2[2].ok ? 'ok' : (onda2[2].erro as Error).message);
  const [a, b, c] = await emOnda<[number | null, string, Valoracao]>([() => leFator(5), () => leRede(), () => leValor()]);
  console.log(a, b, c === VAL, c.valor);
  try {
    await emOnda<[number, string, number]>([() => falha('primeira'), () => leRede(), () => falha('segunda')]);
  } catch (e) {
    console.log('relançou', (e as Error).message);
  }
  const janelas = [true, false, true];
  const itens = await emOndaSettled<boolean[]>(janelas.map((j) => () => ok(j)));
  for (const [i, item] of itens.entries()) console.log(i, item.ok && item.valor);
  const [nome, situacao] = await emOndaSettled<[string, number | undefined]>([() => leRede(), async () => undefined]);
  console.log(valorDaOnda(nome).startsWith('N'), valorDaOnda(situacao) ?? 0);
  const [n1, sup] = await emOndaSettled<[any[] | null, any[]]>([() => Promise.resolve(null), async () => [{ n: 1 }, { n: 2 }]]);
  if (!n1.ok || !n1.valor) console.log('sem nivel1');
  if (sup.ok) console.log(sup.valor.length, sup.valor.filter((x: any) => x.n > 1).length);
  console.log(ordem.join(','));
}
main();
