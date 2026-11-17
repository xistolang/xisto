// a floating call that rejects with nobody to handle it: Node's unhandled rejection, here taken by the
// 'unhandledRejection' listener (the program goes on)
// warns: XS2112
process.on("unhandledRejection", (reason: Error) => {
  console.log("unhandled:", reason.message);
});

function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve) => setTimeout(resolve, ms));
}

async function boom(ms: number): Promise<void> {
  console.log("boom start");
  await sleep(ms);
  throw new Error("boom after " + ms);
}

async function main(): Promise<void> {
  boom(5);
  console.log("after boom");
  await sleep(20);
  console.log("main end");
}

main();
