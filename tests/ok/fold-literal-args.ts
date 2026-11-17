// Literal arguments of a function called once are read as literals in its body (src/fold.c): the branches they
// decide are not compiled. The results must be the same as with the reads kept.
interface Opts {
  style?: string;
  digits?: number;
  grouping?: boolean;
  label?: string | null;
}
class Fmt {
  text: string;
  constructor(locale: string | null = null, options: Opts | null = null) {
    const loc = locale === "pt-BR" ? 1 : locale === "en-US" ? 0 : 2;
    const o: Opts = options ?? {};
    const style = o.style ?? "decimal";
    const digits = o.digits;
    const n: int = digits === null ? 2 : Math.floor(digits);
    let t = "";
    if (style === "currency") t = loc === 1 ? "R$" : "$";
    else if (style === "percent") t = "%";
    else t = "#";
    if (n >= 3 && n * 2 - 1 > 4) t += "+";
    if (!(o.grouping ?? true)) t += "!";
    if (o.label != null) t += o.label;
    this.text = `${t}:${n}:${loc}`;
  }
}
function pick(kind: string, n: number): string {
  if (kind === "a") return `A${n}`;
  if (kind === "b") return `B${n}`;
  return `other ${kind}`;
}
function twice(kind: string): string {
  return kind === "x" ? "X" : "Y";
}
function mutated(n: number): number {
  n += 1;
  return n > 1 ? n : -n;
}
function passOn(o: Opts): string {
  return keep(o);
}
function keep(o: Opts): string {
  return o.style ?? "none";
}
console.log(new Fmt("pt-BR", { style: "currency", digits: 4, grouping: false, label: "L" }).text);
console.log(pick("b", 7), twice("x"), twice("z"), mutated(1), passOn({ style: "s" }));
// a literal's length and code units, and bitwise operators over literals, decided when compiling
function code3(c: string): string {
  const a = c.length === 3 ? c.charCodeAt(0) | 32 : 0;
  const z = "é".length === 1 ? 1 : 0;
  return a === 98 ? `ok ${a & 15} ${a ^ 1} ${z}` : "bad";
}
console.log(code3("BRL"));
