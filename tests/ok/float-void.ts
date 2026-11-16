// `void f();` starts an async call on purpose: the same as a bare f(); (it runs to its first wait, the caller goes
// on) without warning XS2112; so do a chained .catch, a kept promise and an await
// warns: none
function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve) => setTimeout(resolve, ms));
}

async function job(name: string, ms: number): Promise<number> {
  console.log("start", name);
  await sleep(ms);
  console.log("end", name);
  return ms;
}

async function main(): Promise<void> {
  void job("void", 10);
  console.log("after void");
  job("caught", 5).catch((e: Error) => console.log(e.message));
  console.log("after caught");
  const kept = job("kept", 1);
  console.log("awaited", await job("awaited", 2), await kept);
}

void main();
console.log("top level done");
