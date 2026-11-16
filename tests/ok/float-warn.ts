// warning XS2112: an async call used as a statement, not awaited (the build still succeeds; the output is Node's)
// warns: XS2112 2
async function notify(who: string): Promise<void> {
  await new Promise<void>((resolve) => setTimeout(resolve, 1));
  console.log("notified", who);
}

async function main(): Promise<void> {
  notify("ana");
  Promise.resolve(1);
  void notify("bia");
  console.log("main end");
}

void main();
