// build: --tsc-checked
async function primeira(n: int): Promise<{ erro: string | null; linha: int }> { return { erro: null, linha: n }; }
async function main(): Promise<void> {
  let r: Awaited<ReturnType<typeof primeira>>;
  r = await primeira(4);
  console.log(r.linha, r.erro);
  const v: unknown = 5;
  const u: number | string = v as number | string;
  console.log(u);
}
main();
