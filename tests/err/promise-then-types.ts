// expect: return value of 'arrow' must be number, but this is string
async function n(): Promise<number> { return 1; }
async function main(): Promise<void> {
  const v = await n().catch(() => "x");
  console.log(v);
}
main();
