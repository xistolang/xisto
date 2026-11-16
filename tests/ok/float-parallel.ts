// const a = getA(); const b = getB(); await Promise.all([a, b]): both start before either ends (in parallel)
function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve) => setTimeout(resolve, ms));
}

async function getA(): Promise<string> {
  console.log("A start");
  await sleep(30);
  console.log("A end");
  return "a";
}

async function getB(): Promise<number> {
  console.log("B start");
  await sleep(10);
  console.log("B end");
  return 2;
}

async function main(): Promise<void> {
  const t0 = Date.now();
  const a = getA();
  const b = getB();
  console.log("both started");
  const [x, y] = await Promise.all([a, b]);
  const took = Date.now() - t0;
  console.log(x, y, "parallel:", took < 38);
}

main();
