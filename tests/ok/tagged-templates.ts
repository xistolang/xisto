// Tagged templates: tag(strings, ...values) with strings.raw, empty parts, escapes, nested templates, a method as
// the tag, the same strings array on every call of one call site (another site has its own), String.raw.
function parts(strings: TemplateStringsArray, ...values: unknown[]): string {
  return JSON.stringify({ s: [...strings], raw: strings.raw, v: values, n: strings.length });
}
const x = 5, name = "ann";
console.log(parts`${x}${name}`);
console.log(parts``);
console.log(parts`a\tb${x}A${`in${x}`}end`);
class Sql {
  q(strings: TemplateStringsArray, ...vals: number[]): string {
    let out = strings[0];
    for (let i = 0; i < vals.length; i++) out += `$${i + 1}` + strings[i + 1];
    return out;
  }
}
const db = new Sql();
console.log(db.q`select * from t where a = ${1} and b = ${2}`);
function first(): string { return parts`early ${x}`; }
console.log(first());
function count(s: TemplateStringsArray): number { return s.length; }
console.log(count`z`, count`a\u{1F600}b`);
function rawOf(s: TemplateStringsArray, ...v: number[]): readonly string[] { return s.raw; }
const raws: string[] = [];
for (const r of rawOf`one\n${0}two\\`) raws.push(r);
console.log(raws, rawOf`x`.length);
function same(s: TemplateStringsArray): TemplateStringsArray { return s; }
const seen: TemplateStringsArray[] = [];
for (let i = 0; i < 2; i++) seen.push(same`site`);
console.log(seen[0] === seen[1], seen[0] === same`site`, seen[0]);
console.log(String.raw`a\nb${x}`);
