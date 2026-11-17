// An async call not awaited (a floating promise) runs from its call until its first wait; then the caller goes on,
// and the call resumes later from the event loop, as in Node. A callee that never waits runs to its end at the call.
// warns: XS2112
function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve) => setTimeout(resolve, ms));
}

async function sendEmail(to: string, ms: number): Promise<void> {
  console.log("sending to", to);
  await sleep(ms);
  console.log("sent to", to);
}

async function noWait(n: number): Promise<number> {
  console.log("noWait", n);
  return n * 2;
}

async function handle(): Promise<string> {
  sendEmail("ana", 20);
  console.log("after ana");
  sendEmail("bia", 5);
  console.log("after bia");
  noWait(1);
  console.log("after noWait");
  return "ok";
}

async function main(): Promise<void> {
  const r = await handle();
  console.log("handle returned", r);
  await sleep(40);
  console.log("main done");
}

main();
console.log("top level done");
