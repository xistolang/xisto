// rest parameters, Math.max(...xs), nested destructuring, keyof, conditional type aliases
function cat(sep: string, ...xs: string[]): string { return xs.join(sep); }
console.log(cat("-"), cat("-", "a"), cat("-", "a", "b", "c"));
const parts: string[] = ["x", "y"];
console.log(cat("+", ...parts));
const ns: number[] = [3, 9.5, 1];
console.log(Math.max(...ns), Math.min(...ns.map((n) => n * 2)));
const pairs: [string, { n: number; s: string }][] = [["a", { n: 1, s: "one" }], ["b", { n: 2, s: "two" }]];
for (const [k, { n, s }] of pairs) console.log(k, n, s);
const [first, { n: nn }] = pairs[1];
console.log(first, nn);
interface Row { id: number; name: string; ok: boolean }
type K = keyof Row & string;
function pick(r: Row, k: keyof Row): string { return String((r as any)[k]); }
const r: Row = { id: 7, name: "z", ok: true };
console.log(pick(r, "name"), pick(r, "id"));
const k2: K = "ok";
type Same = Row extends Row ? true : never;
const ok: Same = true;
console.log(k2, ok);
