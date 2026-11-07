// async/await are syntax only: compiled as plain calls.
async function compute(n: int): Promise<int> {
  return n * 2;
}

class Worker {
  async run(n: int): Promise<string> {
    const v: int = await compute(n);
    return "v=" + String(v);
  }
}

async function main(): Promise<void> {
  const w: Worker = new Worker();
  console.log(await w.run(21));
  console.log(await compute(1) + (await compute(2)));
}

main();
