// Promise.allSettled: of kept promises (each outcome, in order), of xs.map(cb) (each a task; a synchronous throw made a
// rejection is one), of Promise<void>[] inside Promise.race (a void element settles with undefined)
// build: --tsc-checked
async function ok(n: number): Promise<number> { return n * 2; }
async function falha(n: number): Promise<number> { if (n > 1) throw new Error('boom ' + n); return n; }
async function main1(): Promise<void> {
  const ps: Promise<number>[] = [ok(1), falha(2), ok(3)];
  const rs = await Promise.allSettled(ps);
  for (const r of rs) {
    if (r.status === 'fulfilled') console.log('ok', r.value);
    else console.log('erro', (r.reason as Error).message);
  }
  console.log('fim');
}
function espera(ms: number): Promise<void> { return new Promise((r) => setTimeout(r, ms)); }
async function main2(): Promise<void> {
  let feitos = 0;
  const vs: Promise<void>[] = [];
  for (const n of [1, 2]) vs.push(espera(5 * n).then(() => { feitos++; }));
  await Promise.race([Promise.allSettled(vs), espera(200)]);
  console.log('feitos', feitos);
  const fns: (() => Promise<unknown>)[] = [async () => 1, async () => { throw new Error('x'); }];
  const settled = await Promise.allSettled(
    fns.map((f) => {
      try {
        return f();
      } catch (e) {
        return Promise.reject(e);
      }
    }),
  );
  console.log(settled.map((s) => (s.status === 'fulfilled' ? 'v' : 'e')).join(''));
}
main1().then(() => main2());
