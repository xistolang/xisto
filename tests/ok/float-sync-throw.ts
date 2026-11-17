// an async function that throws before its first wait, called without await: the throw rejects its promise (as in
// Node), so the caller's try/catch does not catch it; with nobody handling the promise it is an unhandled rejection
// (here the 'unhandledRejection' listener takes it). A callee that cannot throw and never waits stays a plain call.
// warns: XS2112
process.on("unhandledRejection", (reason: Error) => {
  console.log("unhandled:", reason.message);
});

async function boom(msg: string): Promise<void> {
  console.log("boom start", msg);
  throw new Error(msg);
}

async function boomLater(msg: string): Promise<number> {
  if (msg.length > 0) {
    throw new Error(msg);
  }
  await null;
  return 1;
}

async function main(): Promise<void> {
  try {
    boom("sync");
    console.log("after boom: the caller goes on");
    boomLater("before the first await");
    console.log("after boomLater");
  } catch (e) {
    console.log("caught here (wrong):", (e as Error).message);
  }
  await new Promise<void>((resolve) => setTimeout(resolve, 5));
  console.log("main end");
}

main();
