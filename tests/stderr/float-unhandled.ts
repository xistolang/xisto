// a floating call that rejects and nobody handles it: an unhandled rejection, as in Node. The program ends with exit
// code 1 after the 'exit' listeners (the error goes to stderr in xisto's format); the caller had gone on meanwhile
process.on("exit", (code: number) => console.log("exit", code));

function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve) => setTimeout(resolve, ms));
}

async function save(ms: number): Promise<void> {
  console.log("save start");
  await sleep(ms);
  throw new Error("disk full");
}

async function handler(): Promise<void> {
  void save(5);
  console.log("handler answered");
  await sleep(50);
  console.log("never");
}

void handler();
console.log("top level done");
