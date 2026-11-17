// a promise kept and awaited later catches its rejection; a .catch on a floating call handles it too
function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve) => setTimeout(resolve, ms));
}

async function fail(msg: string, ms: number): Promise<number> {
  console.log("fail start", msg);
  await sleep(ms);
  throw new Error(msg);
}

async function main(): Promise<void> {
  const p = fail("kept", 10);
  console.log("kept started");
  fail("caught", 5).catch((e: Error) => console.log("catch:", e.message));
  console.log("caught started");
  try {
    await p;
  } catch (e) {
    console.log("await caught:", (e as Error).message);
  }
  console.log("main end");
}

main();
