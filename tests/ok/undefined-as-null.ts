// undefined is a deprecated synonym of null (XS2101): values, comparisons, typeof, void, optional fields left
// absent by undefined. Only the cases whose output Node agrees with (see undefined-as-null-diverge for the others).
// warns: (XS2101
interface User { name: string; nick?: string; age?: number; admin?: boolean }

function find(xs: string[], k: string): string | null {
  for (const x of xs) if (x === k) return x;
  return undefined;
}
function label(n: number | undefined): string { return n === undefined ? "none" : String(n); }
function done(): void { return undefined; }
function stop(): void { return void 0; }

let a: string | null = undefined;
console.log(a == null, a == undefined, a != undefined);
a = "x";
console.log(a === undefined, a !== undefined, a == undefined, a != undefined, typeof a === "undefined", typeof a !== "undefined");
a = undefined;
console.log(a == null, a ?? "empty");
console.log(find(["p", "q"], "q"), find(["p"], "z") ?? "missing", find([], "z") === undefined);
console.log(label(undefined), label(2.5));
done(); stop();
const b: string | null = null;
const c = b ?? undefined;
console.log(c == null, c === undefined);
const t: string | null = b !== null ? "y" : undefined;
console.log(t ?? "t empty");
const v: number | null = void 0;
console.log(v == null, v === undefined, void 0 === undefined);
const xs: (number | null)[] = [1, undefined, void 0];
console.log(xs.length, xs[1] == null, JSON.stringify(xs));

const u: User = { name: "ana", nick: undefined };
console.log(JSON.stringify(u), "nick" in u === false || "nick" in u);
console.log(u.nick === undefined, u.age === undefined, u.nick == null, typeof u.nick, typeof u.age, typeof u.admin);
u.nick = "an"; u.age = 30; u.admin = true;
console.log(JSON.stringify(u), Object.keys(u), u);
console.log(u.nick === undefined, u.age !== undefined, typeof u.nick, typeof u.age, typeof u.admin);
u.nick = undefined; u.age = undefined; u.admin = undefined;
console.log(JSON.stringify(u));
console.log(u.nick === undefined, u.age === undefined, u.admin === undefined, typeof u.age === "undefined");
const w: User = { name: "bia", age: b !== null ? 1 : undefined };
console.log(JSON.stringify(w), w.age === undefined);
