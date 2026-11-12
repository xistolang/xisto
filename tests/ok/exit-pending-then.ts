// build: --tsc-checked
// a .then on a promise nothing can settle any more (its timer was cleared) does not keep the program alive, as in Node;
// a cleared timer ends at once (the program does not wait out its delay)
async function motor(ms: number): Promise<number> {
  await new Promise<void>((ok) => setTimeout(ok, ms));
  return 42;
}
async function main(): Promise<void> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  const sentinela = new Promise<string>((resolve) => {
    timer = setTimeout(() => resolve('teto'), 30000);
  });
  const r = await Promise.race([motor(5).then((n) => ({ k: 'ok', n })), sentinela.then((m) => ({ k: m, n: 0 }))]);
  clearTimeout(timer);
  console.log(r.k, r.n);
  const t = setTimeout(() => console.log('nunca'), 30000);
  clearTimeout(t);
  console.log('fim');
}
main();
