// Promise.all / race over arrays of promises kept (Promise<T>[])
function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
async function one(i: number, ms: number): Promise<string> {
  await sleep(ms);
  console.log("done", i);
  return "v" + String(i);
}
async function main(): Promise<void> {
  const tasks: Promise<string>[] = [];
  for (let i = 0; i < 4; i++) tasks.push(one(i, 20 - i * 5));
  const all = await Promise.all(tasks);
  console.log(all.join(" "));
  const nums: Promise<number>[] = [];
  for (let i = 0; i < 3; i++) nums.push(Promise.resolve(i * 1.5));
  console.log((await Promise.all(nums)).join(","));
  const r: Promise<string>[] = [one(10, 30), one(11, 5)];
  console.log("race", await Promise.race(r));
  await sleep(40);
  const vs: Promise<void>[] = [sleep(3), sleep(1)];
  await Promise.all(vs);
  console.log("voids");
}
main();
