// JSON.parse(text, reviver): values passed from the leaves up (keys in that order, the root as ""), a Date made by the
// reviver stays a Date in an any, undefined deletes a member, and the revived value read into a class
class Ev { name: string; at: Date; n: number; constructor() { this.name = ""; this.at = new Date(0); this.n = 0; } }
const text = '{"name":"x","at":"2020-01-02T03:04:05.000Z","n":2,"extra":[1,{"q":3}]}';
const a: any = JSON.parse(text, (k, v) => k === "at" ? new Date(v) : v);
console.log(a.at instanceof Date, (a.at as Date).getTime(), a.name);
const b: any = JSON.parse(text, (k, v) => typeof v === "number" ? v * 2 : v);
console.log(JSON.stringify(b));
const c: any = JSON.parse(text, (k, v) => k === "extra" ? undefined : v);
console.log(JSON.stringify(c));
const keys: string[] = [];
const _x: any = JSON.parse(text, (k, v) => { keys.push(k); return v; });
console.log(keys.join(","));
const e: Ev = JSON.parse(text, (k, v) => k === "n" ? 40 : v);
console.log(e.name, e.n);
