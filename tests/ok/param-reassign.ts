function obs(ldsObservacao: string, isento: string, n: number): string {
  if (n > 1) ldsObservacao = `Proc ${ldsObservacao}`.substring(0, 8);
  if (n > 2) { isento = 'S'; ldsObservacao = 'INTERCORRENCIA'; }
  return ldsObservacao + "/" + isento;
}
class K { tag(p: string[], s: string): string { p = [...p, s]; s = s + "!"; return p.join(",") + s; } }
const base = "abcdefghijkl";
console.log(obs(base, "N", 1), obs(base, "N", 2), obs(base, "N", 3), base);
const arr = ["x"]; console.log(new K().tag(arr, "y"), arr.length);
