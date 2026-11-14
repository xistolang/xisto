// XB-217: m.get(k) of an absent key with number values (int in xisto): `?? d` gives d, and `const v = m.get(k)` is T | null
interface Order { name: string; id: number }
const counts = new Map<string, number>();
const byId = new Map<number, number>();
const orders: Order[] = [{ name: "a", id: 1 }, { name: "b", id: 2 }];
counts.set("a", 1);
byId.set(2, 5);
for (const o of orders) {
  console.log(counts.get(o.name) ?? 0, byId.get(o.id) ?? -1);
  console.log(counts.get(o.name + "x") ?? 0, byId.get(o.id + 1) ?? -1);
}
for (const w of ["a", "b", "a", "c"]) counts.set(w, (counts.get(w) ?? 0) + 1);
console.log(counts.get("a"), counts.get("b"), counts.get("c"), counts.get("zz") ?? 0);
const absent = counts.get("zz");
if (absent === undefined) console.log("absent");
const present = counts.get("a");
if (present !== undefined) console.log("present", present + 1);
class Tally { seen = new Map<string, number>(); of(k: string): number { return this.seen.get(k) ?? 0; } }
console.log(new Tally().of("nope"));
const names = new Map<string, string>();
console.log(names.get("x") ?? "none");
