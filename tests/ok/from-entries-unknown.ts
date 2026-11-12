// Object.fromEntries of [key, value] pairs whose value is of any type (an unknown[] each), merged into a Record
const raw: Record<string, unknown> = { a: 1, b: "x", c: null };
const fields = ["a", "b", "c"];
const o = Object.fromEntries(fields.map((f) => [f, raw[f]]));
const base: Record<string, unknown> = { id: 7 };
const merged = { ...base, ...o };
console.log(JSON.stringify(o), JSON.stringify(merged));
