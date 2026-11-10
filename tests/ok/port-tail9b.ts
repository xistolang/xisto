// build: --tsc-checked
const xs = ["a", "b", "c"];
for (const [i, x] of xs.entries()) console.log(i, x);
let w: number | null = null;
w = 2.5;
const total = 1;
if (total + w > 3) console.log("big", total + w);
const u: unknown = 4;
console.log((u as number) * 2);
async function run<T>(fn: () => Promise<T>): Promise<T> {
  const r = await fn();
  console.log("ran");
  return r;
}
async function nada(): Promise<void> { console.log("nada"); }
async function main(): Promise<void> { await run(nada); const n = await run(async () => 3); console.log(n); }
const opts = { a: "dflt" };
const mk = (p: string, ilm: string = opts.a) => p + ilm;
console.log(mk("x"), mk("y", "z"));
main();
