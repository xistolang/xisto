const fila = [1, 2, 3, 4, 5];
const feitos: number[] = [];
const trabalhador = async (): Promise<void> => {
  while (fila.length > 0) {
    const x = fila.shift()!;
    await new Promise<void>((ok) => setTimeout(ok, 1));
    feitos.push(x * 10);
  }
};
async function main(): Promise<void> {
  await Promise.all(Array.from({ length: 2 }, trabalhador));
  console.log(feitos.length, feitos.reduce((a, b) => a + b, 0));
  fila.push(7, 8);
  await Promise.all(Array.from({ length: 3 }, () => trabalhador()));
  console.log(feitos.length);
  const ss = Array.from({ length: 3 }, (_, i) => `:s${i}`).join(", ");
  console.log(ss);
}
main();
