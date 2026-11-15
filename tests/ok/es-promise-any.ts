// Promise.withResolvers (ES2024), Promise.any (ES2021) and AggregateError
function delay(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms));
}
async function value(ms: number, v: number): Promise<number> {
  await delay(ms);
  return v;
}
async function fail(ms: number, msg: string): Promise<number> {
  await delay(ms);
  throw new Error(msg);
}
async function main(): Promise<void> {
  const { promise, resolve } = Promise.withResolvers<string>();
  setTimeout(() => resolve("resolved later"), 5);
  console.log(await promise);
  const w = Promise.withResolvers<number>();
  w.promise.then((v) => console.log("then", v));
  w.resolve(42);
  await w.promise;
  const r = Promise.withResolvers<number>();
  r.reject(new Error("no luck"));
  try {
    await r.promise;
  } catch (e) {
    console.log("rejected:", (e as Error).message);
  }
  // the first to fulfil wins, even after a rejection
  console.log(await Promise.any([fail(5, "a"), value(30, 1), value(15, 2)]));
  const ps: Promise<number>[] = [value(20, 7), fail(1, "b")];
  console.log(await Promise.any(ps));
  try {
    await Promise.any([fail(10, "x"), fail(5, "y")]);
  } catch (e) {
    console.log(e instanceof AggregateError, e instanceof Error, (e as Error).name, (e as Error).message);
    if (e instanceof AggregateError) console.log(e.errors.length, (e.errors[0] as Error).message, (e.errors[1] as Error).message);
  }
  const agg = new AggregateError([new Error("one"), new Error("two")], "several");
  console.log(agg.name, agg.message, agg.errors.length, agg instanceof Error);
  const plain = new AggregateError([]);
  console.log(JSON.stringify(plain.message), plain.errors.length);
  try {
    throw agg;
  } catch (e) {
    console.log(String(e));
  }
}
main();
