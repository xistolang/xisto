// XB-231: a function not written async but declared Promise<T> hands back the promise it returns. Where the call's
// promise is kept (.then, a variable, a comparison, an argument, Promise.all/race) it is that promise itself: the
// same object as the one kept in a map, and a promise that never settles leaves nothing parked (0 alive at exit)
const pending = new Map<string, Promise<number>>();
class Resolver {
  resolve: (v: number) => void;
  constructor(resolve: (v: number) => void) {
    this.resolve = resolve;
  }
}
const resolvers = new Map<string, Resolver>();

function hold(k: string): Promise<number> {
  const w = new Promise<number>(() => {});
  pending.set(k, w);
  return w;
}

function later(k: string): Promise<number> {
  const w = new Promise<number>((resolve) => {
    resolvers.set(k, new Resolver(resolve));
  });
  pending.set(k, w);
  return w;
}

function settle(k: string, v: number): void {
  const r = resolvers.get(k);
  if (r) r.resolve(v);
}

// generic: the instance's version
function wrap<T>(v: T): Promise<T> {
  return Promise.resolve(v);
}

function twice(k: string): Promise<number> {
  return later(k).then((v) => v * 2);
}

function same(p: Promise<number>, q: Promise<number>): boolean {
  return p === q;
}

// never settles: nothing waits on it at exit
hold("x").then((v) => console.log("never", v));
console.log("identity", hold("y") === pending.get("y")!);
const kept = hold("z");
console.log("kept", kept === pending.get("z")!, same(hold("w"), pending.get("w")!));

later("a")
  .then((v) => v + 1)
  .then((v) => console.log("a chain", v));
const tw = twice("b");
wrap<string>("generic").then((s) => console.log("wrap", s));
wrap(41).then((n) => console.log("wrap", n + 1));

async function main(): Promise<void> {
  settle("a", 10);
  settle("b", 21);
  console.log("twice", await tw);
  console.log("awaited", await wrap("direct"));
  setTimeout(() => settle("c", 3), 1);
  const both = await Promise.all([later("c"), wrap(2)]);
  console.log("all", both.length);
  const c = later("d");
  setTimeout(() => settle("d", 7), 5);
  console.log("race", await Promise.race([c, hold("e")]));
  console.log("map has", pending.size);
}
main();
