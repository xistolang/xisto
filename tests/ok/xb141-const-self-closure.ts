// XB-141: a const with no written type whose initializer's closure reads the const itself (.finally guard)
let cur0: Promise<number> | null = null;
function f(): Promise<number> { return Promise.resolve(7); }
const x0 = f().finally(() => { if (cur0 === x0) { cur0 = null; console.log("cleared"); } });
cur0 = x0;

class Loader {
  private cur: Promise<string> | null = null;
  private fetch(): Promise<string> { return Promise.resolve("data"); }
  load(): Promise<string> {
    if (this.cur) return this.cur;
    const p = this.fetch().finally(() => {
      if (this.cur === p) { this.cur = null; console.log("released"); }
    });
    this.cur = p;
    return p;
  }
}

async function run(): Promise<void> {
  let pending: Promise<number> | null = null;
  const x = Promise.resolve(3).then((v) => v * 2).finally(() => { if (pending === x) { pending = null; console.log("done x"); } });
  pending = x;
  console.log("x", await x, pending === null);
}

async function main(): Promise<void> {
  console.log("v", await x0, cur0 === null);
  const l = new Loader();
  console.log(await l.load());
  await run();
}
main();
