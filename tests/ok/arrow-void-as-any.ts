// an arrow with no value at its end, where () => any goes (a tracer's trace(name, () => {...})): it gives null, as
// JavaScript's undefined; async and valued callbacks pass through any as they are
function sleep(ms: number): Promise<void> { return new Promise<void>((r) => setTimeout(r, ms)); }
function traced(name: string, fn: () => any): any {
  console.log("start", name);
  const r: any = fn();
  console.log("end", name);
  return r;
}
traced("void", () => { console.log("in void"); });
traced("void-if", () => { if (Math.random() > 2) return 1; console.log("no return"); });
console.log(traced("num", (): number => 5));
async function main(): Promise<void> {
  const s: string = await traced("async", async (): Promise<string> => { await sleep(3); return "a"; });
  console.log(s);
}
main();
