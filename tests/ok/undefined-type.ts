// The type undefined (transform(): undefined, a Nest pipe): its value is written as Node writes undefined (console.log,
// String, templates; left out of JSON objects)
class SemQuery {
  vezes = 0;
  transform(value: unknown): undefined {
    this.vezes++;
    if (value !== null && typeof value === 'object' && Object.keys(value).length > 0) throw new Error('extra');
    return undefined;
  }
}
const s = new SemQuery();
const r = s.transform({});
console.log(s.transform(null), r, `t=${s.transform(null)}`, String(r), String(s.transform({})), s.vezes);
console.log([r].length, r === undefined, r == null, JSON.stringify({ a: 1, r }));
function nada(): undefined { return; }
console.log(nada(), `${nada()}!`);
const rr: undefined = undefined; const o = { a: 1, rr }; console.log(o); console.log(JSON.stringify(o), JSON.stringify([rr]));
