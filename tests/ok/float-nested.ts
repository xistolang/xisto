// floating calls inside floating calls, and in a loop: each starts, runs to its first wait and lets its caller go on
// warns: XS2112
function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve) => setTimeout(resolve, ms));
}

async function leaf(name: string, ms: number): Promise<void> {
  console.log("leaf start", name);
  await sleep(ms);
  console.log("leaf end", name);
}

async function inner(name: string): Promise<void> {
  console.log("inner start", name);
  leaf(name + ".1", 15);
  console.log("inner after leaf", name);
  await sleep(5);
  leaf(name + ".2", 1);
  console.log("inner end", name);
}

async function outer(): Promise<void> {
  inner("x");
  console.log("outer after inner");
  for (let i = 0; i < 3; i++) {
    leaf("loop" + i, 30 - i * 10);
    console.log("loop", i);
  }
  console.log("outer end");
}

outer();
console.log("top");
setTimeout(() => console.log("timer 50"), 50);
