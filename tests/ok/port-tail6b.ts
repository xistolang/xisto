// build: --tsc-checked
// warns: XS2105
// warns: XS2107
// warns: XS2109
// appa round 6 under --tsc-checked: a number as a condition, T | null in a comparison, s[i], xs[n] with n a
// number, o[k] += v on an any, for...of an any, a ?: of an any and null, a primitive union read from JSON

function tag(n: number): string {
  return n ? `n=${n}` : "zero";
}
console.log(tag(0), tag(2.5), tag(NaN));

function menor(x: number | null): boolean {
  return x < 1;
}
console.log(menor(0.5), menor(3), menor(null));
function grande(xs: string[] | null): boolean {
  return xs?.length > 0;
}
console.log(grande(["a"]), grande([]), grande(null));

const palavra = "xisto";
console.log(palavra[0], palavra[4]);

const meses = [31, 28, 31, 30];
const m: number = 3;
console.log(meses[m - 1]);

const resumo: any = { erro: 0, aviso: 0 };
const kinds = ["erro", "aviso", "erro"];
for (const k of kinds) resumo[k] += 1;
console.log(resumo.erro, resumo.aviso);

const linhas: any = JSON.parse('[{"N":1},{"N":2}]');
let soma = 0;
for (const l of linhas) soma += Number(l.N);
console.log("soma", soma);

function primeiro(r: any, ok: boolean): any {
  return ok ? r : null;
}
console.log(primeiro(5, true), primeiro(5, false));

interface Linha {
  QTD: number | string | null;
}
const rows: Linha[] = JSON.parse('[{"QTD":3},{"QTD":"7"},{"QTD":null}]');
console.log(rows.map((r) => Number(r.QTD)).join(","), JSON.stringify(rows));
