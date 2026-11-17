// (deeper than Node allows: the expected output is checked in) main(); with more top-level code after it: main runs until its first wait, the next top-level line runs, then
// main goes on (Node's order) on a stack as large as the main thread's: deep recursion that worked when main ran
// on the main stack still works
function depth(n: number, a: number, b: string): number {
  if (n === 0) return a + b.length;
  const x = a * 1.5 + n;
  const s = b.length > 8 ? b.slice(1) : b + "x";
  return depth(n - 1, x - a * 1.5 - n + 1, s) + 1;
}

async function main(): Promise<void> {
  console.log("main start");
  await new Promise<void>((resolve) => setTimeout(resolve, 1));
  console.log("depth", depth(50000, 0, "a"));
}

main();
console.log("top level after main");
