// sort() / toSorted() without a comparator (String() forms by UTF-16 code units), Object.assign into a new
// object literal, Promise.reject mixed with other promises in Promise.any/all/race/allSettled
const a = [3, 1, 10, 2];
console.log(a.toSorted(), [...a].sort(), ["b", "a"].sort(), a);
console.log([true, false, true].sort(), [10n, 9n, 100n].sort(), [0.5, -1, 1e21, 2].sort());
console.log(["\u{1F600}", "Ａ", "a", "B", "é"].sort());
const u: unknown[] = [3, "10", 2, "a"];
console.log(u.sort());
const n: (number | null)[] = [3, null, 1];
console.log(n.sort());
const o: Record<string, number> = { a: 1 };
const c = Object.assign({}, o);
c["z"] = 9;
console.log(Object.assign({}, o), c, o);
const o2: Record<string, number> = { b: 2, a: 3 };
console.log(Object.assign({}, o, o2));
class P { x = 1; y = "s"; }
const p = new P();
const q = Object.assign({}, p);
console.log(q, q.x, Object.assign({ x: 0, w: true }, p));
console.log(["b10", "b9", "B1", "a"].toSorted(), [-5, -10, 5].toSorted(), [1, "1", 1].sort());
async function main(): Promise<void> {
  console.log("any", await Promise.any([Promise.reject(new Error("a")), Promise.resolve(2)]));
  try {
    await Promise.all([Promise.resolve(1), Promise.reject(new Error("b"))]);
  } catch (e) {
    console.log("all", (e as Error).message);
  }
  try {
    await Promise.race([Promise.reject(new Error("c")), Promise.resolve(3)]);
  } catch (e) {
    console.log("race", (e as Error).message);
  }
  const rs = await Promise.allSettled([Promise.reject(new Error("d")), Promise.resolve(4)]);
  console.log("settled", rs.map((r) => r.status));
}
main();
