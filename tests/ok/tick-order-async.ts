// Microtask turns of promises kept from async functions, as Node's: the body runs until its first await and the
// caller goes on (an await of a value that is no promise or of a settled promise takes a turn), then/finally/catch
// interleave with them (XB-254: finally settles its promise two turns after its callback, as Node's)
function out(s: string): void {
  console.log(s);
}
Promise.resolve(1)
  .finally(() => out("f1"))
  .then(() => out("f1-then"));
Promise.resolve(2)
  .then(() => out("a1"))
  .then(() => out("a2"))
  .then(() => out("a3"))
  .then(() => out("a4"))
  .then(() => out("a5"));
Promise.reject(new Error("x"))
  .finally(() => out("f2"))
  .catch(() => out("f2-catch"));
async function af(): Promise<number> {
  out("af-body");
  await null;
  out("af-after-await");
  return 3;
}
af().then(() => out("af-then"));
async function ag(): Promise<number> {
  return await Promise.resolve(4);
}
ag().then(() => out("ag-then"));
const pr = new Promise<number>((res) => res(5));
pr.then(() => out("pr-then"));
async function kept(): Promise<string> {
  const p: Promise<string> = (async () => "seven")();
  const v = await p;
  return v;
}
async function nada(): Promise<void> {
  out("nada ran");
}
async function protege(fn: () => Promise<void>): Promise<void> {
  return await fn();
}
kept().then((s) => out(s));
protege(() => nada()).then(() => out("protected"));
Promise.resolve(6)
  .then(async (v: number): Promise<number> => v + 1)
  .then((v) => out(`async callback ${v}`));
out("sync-end");
