// expect: the promise gives number, and its rejection handler string
async function n(): Promise<number> { return 1; }
async function main(): Promise<void> {
  const v = await n().catch(() => "x");
  console.log(v);
}
main();
