// XB-171: Promise.all(xs.map(f)) kept as a promise (.then on it, a const, an element of Promise.race): the map runs at
// once, each f(x) a task, and the promise settles with the values in order (or the first rejection)
async function double(x: number): Promise<number> {
  await new Promise<void>((r) => { setTimeout(r, 3 - x); });
  console.log("double", x);
  return x * 2;
}
async function check(x: number): Promise<number> {
  await new Promise<void>((r) => { setTimeout(r, 1); });
  if (x > 2) throw new Error("too big: " + x);
  return x;
}
const xs: number[] = [1, 2, 3];
Promise.all(xs.map(double)).then((vs) => console.log("then", vs.join(",")));
console.log("after then");
async function main(): Promise<void> {
  await new Promise<void>((r) => { setTimeout(r, 10); });
  const k = 10;
  const local: number[] = [4, 5];
  const kept = Promise.all(local.map(async (x) => (await double(x - 3)) + k));
  console.log("kept made");
  console.log("kept", (await kept).join(","));
  const timeout = new Promise<number[]>((resolve) => { setTimeout(() => resolve([]), 1000); });
  const r = await Promise.race([Promise.all(xs.map((x) => double(x - 1))), timeout]);
  console.log("race", r.join(","));
  await Promise.all(xs.map(check)).then((vs) => console.log("not here", vs.length)).catch((e) => console.log("caught", (e as Error).message));
  console.log("end");
}
main();
