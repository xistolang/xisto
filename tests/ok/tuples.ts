// Tuples and destructuring (XB-025): [A, B] types, const [a, , c] = e, const { a, b: x } = e, patterns in
// for...of and in parameters, Map entries and for...of over a Map or Set, new Map/Set(initial), Array.from,
// Promise.all of different types, and let a = 1, b = 2
function minmax(xs: number[]): [number, number] {
  let lo = xs[0], hi = xs[0];
  for (const x of xs) { lo = Math.min(lo, x); hi = Math.max(hi, x); }
  return [lo, hi];
}
const [lo, hi] = minmax([3, 1, 4, 1, 5]);
console.log(lo, hi);
const t: [string, int] = ["age", 42];
console.log(t[0], t[1], t.length, t);
const pairs: [string, int][] = [["a", 1], ["b", 2]];
for (const [k, v] of pairs) { console.log(k, v); }
console.log(pairs, JSON.stringify(pairs));
const back: [string, int][] = JSON.parse(JSON.stringify(pairs));
console.log(back[1][0], back[1][1]);
const letters: string[] = ["x", "y", "z"];
const [first, , third] = letters;
console.log(first, third);
interface P { x: number; y: number; name: string; }
const p: P = { x: 1, y: 2, name: "pt" };
const { x, y: why, name } = p;
console.log(x, why, name);

const m = new Map<string, int>([["a", 1], ["b", 2], ["c", 3]]);
m.delete("b");
for (const [k, v] of m) { console.log(k, v); }
console.log(Array.from(m.entries()).map(([k, v]) => `${k}=${v}`), m);
const byId = new Map<int, string[]>();
byId.set(7, ["x", "y"]);
byId.set(3, []);
for (const [id, names] of byId) { console.log(id, names.length, names); }
const uniq = new Set<string>(["b", "a", "b", "c", "a"]);
for (const u of uniq) { console.log(u); }
console.log(uniq.size, Array.from(uniq), Array.isArray(letters));
const scores = new Map<string, number>([["ann", 1.5], ["bob", 2], ["ann", 3]]);
console.log(scores, scores.get("ann"));

interface Pt { x: number; y: number; }
function norm({ x, y }: Pt): number { return Math.sqrt(x * x + y * y); }
function swap([a, b]: [string, int]): [int, string] { return [b, a]; }
const pts: Pt[] = [{ x: 1, y: 2 }, { x: 3, y: 4 }];
console.log(norm({ x: 3, y: 4 }), swap(["q", 9]), pts.map(({ x }) => x * 10), pts.filter(({ y }) => y > 2).length);
class Grid {
  cells: [int, int][] = [[0, 1], [2, 3]];
  sum(): int { let s: int = 0; for (const [a, b] of this.cells) { s += a * b; } return s; }
  at([r, c]: [int, int]): int { return r * 10 + c; }
}
const g = new Grid();
console.log(g.sum(), g.at([4, 2]));

interface User { id: int; name: string; }
async function getUser(id: int): Promise<User> { return { id: id, name: `u${id}` }; }
async function getPosts(id: int): Promise<string[]> { return [`p${id}a`, `p${id}b`]; }
async function getScore(): Promise<number> { return 9.5; }
async function main(): Promise<void> {
  const [user, posts, score] = await Promise.all([getUser(7), getPosts(7), getScore()]);
  console.log(user.name, posts.length, posts, score);
  const [a, b] = await Promise.all([getScore(), getScore()]);
  console.log(a + b);
  const both: [User, number] = await Promise.all([getUser(1), getScore()]);
  console.log(both);
}
main();
