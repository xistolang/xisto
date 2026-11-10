// Promise values (XB-106): kept in variables and fields, then/catch/finally, new Promise, await of a stored promise
function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
async function compute(n: number): Promise<number> {
  console.log("compute start", n);
  await sleep(10);
  console.log("compute end", n);
  return n * 2;
}
async function fails(): Promise<string> {
  await sleep(5);
  throw new Error("boom");
}
class Store {
  cache: number = 0;
  reloading: Promise<void> | null = null;
  async read(): Promise<number> {
    await sleep(5);
    return 42;
  }
  refresh(): void {
    if (this.reloading) return;
    this.reloading = this.read()
      .then((v) => {
      this.cache = v;
    })
      .catch(() => undefined)
      .finally(() => {
      this.reloading = null;
      console.log("refreshed", this.cache);
    });
  }
}
async function main(): Promise<void> {
  const p = compute(1);
  console.log("after call");
  const q = compute(2);
  console.log(await p, await q, await p);
  const r = await fails().catch((e: Error) => "caught " + e.message);
  console.log(r);
  const s = new Store();
  s.refresh();
  s.refresh();
  await s.reloading;
  console.log("cache", s.cache);
  Promise.resolve(1).then((v) => console.log("then", v));
  console.log("sync");
  await sleep(1);
  const t = Promise.resolve(5);
  console.log(await t);
  const [a, b] = await Promise.all([compute(3), p]);
  console.log(a, b);
  try {
    await fails();
  } catch (e) {
    console.log("try caught", (e as Error).message);
  }
  const w = new Promise<string>((res, rej) => {
    setTimeout(() => res("timer"), 5);
  });
  console.log(await w);
}
main();
