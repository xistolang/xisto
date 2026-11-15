// XB-231: a function declared Promise<T> without async runs its body to the end before the caller goes on, as in
// JavaScript: an async call written as a statement inside it is started and not waited for (the caller's next line
// comes before that call's end), whether the call's promise is kept or awaited; and a throw before its return is
// thrown to the caller (no promise is made)
const pending = new Map<string, Promise<number>>();

function sleep(ms: number): Promise<void> {
  return new Promise<void>((r) => {
    setTimeout(r, ms);
  });
}

async function tick(label: string): Promise<void> {
  console.log(label + " tick start");
  await sleep(10);
  console.log(label + " tick end");
}

function hold(k: string): Promise<number> {
  tick(k);
  console.log("after tick " + k);
  const w = new Promise<number>((r) => {
    setTimeout(() => r(k.length), 30);
  });
  pending.set(k, w);
  return w;
}

function checked(n: number): Promise<number> {
  if (n < 0) throw new Error("negative: " + n);
  return Promise.resolve(n * 10);
}

function viaClosure(k: string): Promise<string> {
  // the closure's own awaits run when the promise's executor calls it, not in this body
  return new Promise<string>((resolve) => {
    const run = async (): Promise<void> => {
      await sleep(5);
      resolve(k + "!");
    };
    run();
  });
}

hold("a").then((v) => console.log("a got", v));
console.log("top goes on", pending.has("a"));

try {
  checked(-1).then((v) => console.log("never", v));
} catch (e) {
  console.log("thrown at the call:", (e as Error).message);
}
checked(2).then((v) => console.log("checked", v));

async function main(): Promise<void> {
  const v = await hold("bb");
  console.log("awaited", v);
  const p = hold("ccc");
  console.log("kept", p === pending.get("ccc")!);
  console.log("value", await p);
  try {
    await checked(-5);
  } catch (e) {
    console.log("caught", (e as Error).message);
  }
  console.log(await viaClosure("closure"));
  viaClosure("then").then((s) => console.log(s));
}
main();
