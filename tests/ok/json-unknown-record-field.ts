// JSON.stringify of an unknown holding an object with a Record<string, unknown> field
class Box { name = "b"; tags: Record<string, unknown> = {}; }
const b = new Box(); b.tags["x"] = 1; b.tags["y"] = ["a", "b"];
const u: unknown = b;
console.log(JSON.stringify(u));
const o: unknown = { q: b.tags, n: 2 };
console.log(JSON.stringify(o));
