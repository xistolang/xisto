// ordering: microtasks, timers, finally, chains, two-argument then, values of each kind
function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
async function num(x: number): Promise<number> { return x; }
async function int(x: number): Promise<boolean> { return x > 1; }
async function label(x: string): Promise<string> { await sleep(1); return "L" + x; }
async function bad(m: string): Promise<number> { throw new Error(m); }

async function main(): Promise<void> {
  setTimeout(() => console.log("timer 0"), 0);
  Promise.resolve(1).then((v) => console.log("micro a", v));
  Promise.resolve(2).then((v) => console.log("micro b", v));
  console.log("sync 1");
  const chained = num(1)
    .then((v) => v + 1)
    .then((v) => v * 10)
    .finally(() => console.log("finally runs"));
  console.log("chained", await chained);
  const two = bad("nope").then(
    (v) => "ok " + String(v),
    (e: Error) => "err " + e.message,
  );
  console.log(await two);
  const b = int(5);
  console.log("bool", await b);
  const ls = [label("x"), label("y")];
  const all = await Promise.all(ls);
  console.log(all.join(","));
  const won = await Promise.race([label("slow"), sleep(0).then(() => "fast")]);
  console.log("race", won);
  const f = bad("in finally").finally(() => console.log("cleanup"));
  try {
    await f;
  } catch (e) {
    console.log("caught after finally:", (e as Error).message);
  }
  const caughtNum = await bad("x").catch(() => -1);
  console.log("caught num", caughtNum);
  let p: Promise<number> = num(0);
  p = num(7);
  console.log("p", await p);
  const stored = label("z");
  await sleep(5);
  console.log("again", await stored, await stored);
  console.log("end of main");
}
main();
