// an object of a made-up shape where an interface with those fields goes (TypeScript is structural)
interface Ref { screen: string; ruleId: string; note?: string; }
const RULE = (ruleId: string, d: string) => ({ screen: 'S1', ruleId, extra: d });
function show(...refs: Ref[]): void {
  for (const r of refs) console.log(r.screen, r.ruleId, r.note);
}
show(RULE('A', 'x'), RULE('B', 'y'));
const one: Ref = RULE('C', 'z');
console.log(one.ruleId);
const list: Ref[] = [RULE('D', 'w')];
console.log(list.length, list[0].screen);
function mk(): { screen: string; ruleId: string } { return { screen: 'T', ruleId: 'E' }; }
const two: Ref | null = mk();
console.log(two?.ruleId);
// a callback parameter written wider than what it is given
function each(xs: string[], f: (s: string) => string): void { for (const x of xs) console.log(f(x)); }
each(['a', 'b'], (s: string | null): string => (s === null ? '-' : s + '!'));
each(['c'], (_: unknown): string => 'u');
// Number, String, Boolean as callbacks; undefined reads as null
console.log(['1', '2.5', 'x'].map(Number), [1, 2].map(String), ['', 'a'].filter(Boolean));
const maybe = (n: number): number | null => (n > 0 ? n : undefined) ?? null;
console.log(maybe(3), maybe(-1));
// JSON read as an any, rows[0]?.FIELD
const rows: any = JSON.parse('[{"VALOR": 12.5, "NOME": "a"}]');
console.log(Number(rows?.[0]?.VALOR) || 0, rows?.[0]?.NOME ?? null, rows?.[1]?.NOME ?? 'none');
const raw: object = JSON.parse('[{"N": 3}]');
console.log(raw[0].N);
// process.env[name]
const nome = 'XISTO_TEST_UNSET_VAR';
console.log(process.env[nome] ?? 'unset');
const d: unknown = new Date(0);
console.log((d as Date).toISOString());
