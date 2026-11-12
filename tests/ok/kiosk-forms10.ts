// forms of the kiosk (round 7): calls typed by an overload's signature (juntas), a function type in a union (comBackstop),
// switch over T | null, one-character lookahead in a RegExp, c ? Promise.resolve({...}) : load(), Dates compared,
// ...(c && { k }), c ? v : undefined absent in JSON, for...of an unknown, (await p).length
// build: --tsc-checked
interface Grupo { CD_PROCEDIMENTO: number; FL: string | null; }
function juntas<A, B>(a: () => A | Promise<A>, b: () => B | Promise<B>): [Promise<A>, Promise<B>];
function juntas<A, B, C>(a: () => A | Promise<A>, b: () => B | Promise<B>, c: () => C | Promise<C>): [Promise<A>, Promise<B>, Promise<C>];
function juntas(...fns: Array<() => unknown>): Array<Promise<unknown>> {
  return fns.map((fn) => {
    let p: Promise<unknown>;
    try {
      p = Promise.resolve(fn());
    } catch (err) {
      p = Promise.reject(err instanceof Error ? err : new Error(String(err)));
    }
    void p.catch(() => undefined);
    return p;
  });
}
async function grupo(n: number): Promise<Grupo | null> { return n > 0 ? { CD_PROCEDIMENTO: n, FL: 'S' } : null; }
async function rows(): Promise<{ QTD: number }[]> { return [{ QTD: 2 }]; }
function texto(): string { return 'sync'; }
async function main_ov(): Promise<void> {
  const [pG, pR] = juntas(() => grupo(3), () => rows());
  const g = await pG;
  const r = await pR;
  console.log(g?.CD_PROCEDIMENTO ?? 0, r && (r[0]?.QTD ?? 0) > 0);
  const [a, b, c] = juntas(() => grupo(0), () => texto(), () => rows());
  console.log(await a, (await b).length, (await c).length);
}
await main_ov();
interface Sinal { abandonado: boolean; }
async function comBackstop<T>(trabalho: Promise<T> | ((sinal: Sinal) => Promise<T>), ms: number): Promise<T> {
  const sinal: Sinal = { abandonado: false };
  const promise: Promise<T> = typeof trabalho === 'function' ? (async () => trabalho(sinal))() : trabalho;
  return promise;
}
async function work(s: Sinal): Promise<string> { return 'ok ' + s.abandonado; }
async function main_un(): Promise<void> {
  console.log(await comBackstop((sinal) => work(sinal), 10));
  console.log(await comBackstop(work({ abandonado: true }), 10));
}
await main_un();
interface Flags { flLeitorDigital: boolean; flCameraFacial: boolean; }
async function flags(n: string): Promise<Flags> { return { flLeitorDigital: n.length > 1, flCameraFacial: true }; }
async function banco(n: string): Promise<string> { return 'hap-' + n; }
async function cfg(tipo: string, n: string): Promise<string> {
  const [f, b] = await Promise.all([
    tipo === 'TABLET' ? Promise.resolve({ flLeitorDigital: false, flCameraFacial: false }) : flags(n),
    banco(n),
  ]);
  return `${f.flLeitorDigital} ${f.flCameraFacial} ${b}`;
}
async function main_tern(): Promise<void> { console.log(await cfg('TABLET', 'x'), await cfg('KIOSK', 'xy')); }
await main_tern();
function fuso(p: number | null): string {
  switch (p) {
    case 1: return 'um';
    case 2: return 'dois';
    default: return 'outro';
  }
}
function tipo(t: string | null): string {
  switch (t) {
    case 'A': return 'a';
    default: return 'def';
  }
}
console.log(fuso(1), fuso(2), fuso(null), fuso(3), tipo('A'), tipo(null), tipo('B'));
const r1 = /((?:src|href)\s*=\s*")\/(?!\/)/g;
  const html = '<img src="/a.png"><a href="//cdn/x">x</a><script src = "/js/app.js"></script><link href="/">';
  console.log(html.replace(r1, '$1https://host/'));
  const r2 = /^\/(?!\/)[^?#\s]*$/;
  for (const t of ['/', '/a/b', '//x', '/a?b', 'a', '/a b', '/x/'])
  console.log(t, r2.test(t));
  const r3 = /a(?=b)/g;
  console.log('ab ac ab'.replace(r3, 'X'), 'xab'.search(r3), /q(?!u)/.exec('quit qat')?.index);
  const r5 = /(\w+)(?=\.)/g;
  console.log('a.b c.d e'.match(r5)?.join('|'));
  console.log('x/y//z/'.split(/\/(?!\/)/).join(','));
  const a = new Date(1000), b = new Date(2000);
  const n: Date | null = Math.random() > 2 ? null : new Date(3000);
  console.log(a < b, a > b, n && n > b, b >= a);
  function itera(lista: unknown): number {
    let s = 0;
    if (Array.isArray(lista)) for (const x of lista) s += Number(x);
    return s;
  }
  console.log(itera([1, 2, '3']), itera('x'));
  const k = 4 / 2;
  console.log(new Uint8Array(k).length);
  interface In { a: string; rot?: number; q?: boolean; }
  function body(i: In): string {
    return JSON.stringify({ a: i.a, ...(i.rot !== undefined && { rot: i.rot }), ...(i.q === true && { q: true }) });
  }
  console.log(body({ a: 'x' }), body({ a: 'y', rot: 90, q: true }));
  function fu(x?: number): string { const o = { a: 1, rot: x !== undefined ? x : undefined }; return JSON.stringify(o); }
  console.log(fu(), fu(2));
  async function rowsAw(): Promise<number[]> { return [1, 2]; }
  async function main_aw(): Promise<void> {
    const p: Promise<number[]> = rowsAw();
    console.log((await p).length);
  }
  await main_aw();
