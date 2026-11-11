// Promise.resolve(null) as the other branch of a conditional promise, inside Promise.all
async function load(id: number): Promise<number[]> {
  return [id, id * 2];
}
async function main(): Promise<void> {
  for (const flag of [true, false]) {
    const [a, b] = await Promise.all([flag ? load(1) : Promise.resolve(null), load(3)]);
    console.log(a === null ? "skipped" : a.join(","), b.join(","));
  }
}
main();
