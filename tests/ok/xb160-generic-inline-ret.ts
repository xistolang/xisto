// XB-160: a generic arrow (and generic function and method) whose written return type is an object type in place
// ({ n: number; b: Record<string, T> }): one interface per instance, in this module and imported from another
import { group as groupLib, split, Repo } from "./lib/xb160-generic-inline-ret.ts";
const group = <T>(xs: T[], key: (x: T) => string): { n: number; b: Record<string, T> } => {
  const b: Record<string, T> = {};
  for (const x of xs) b[key(x)] = x;
  return { n: xs.length, b };
};
interface U { id: number }
const g = group([1, 2, 3], (x) => `k${x}`);
console.log(g.n, g.b["k2"]);
const h = group(["a", "bb"], (x) => x);
console.log(h.n, h.b["bb"], JSON.stringify(h.b));
const gl = groupLib<U>([{ id: 1 }, { id: 2 }], (u) => `u${u.id}`);
console.log(gl.n, gl.b["u2"].id, Object.keys(gl.b).join(","));
const s = split([1, 2, 3]);
console.log(s.head, s.rest.length);
const p = new Repo().page(["a", "b", "c"], 2);
console.log(p.items.join(","), p.total);
const p2 = new Repo().page<U>([{ id: 9 }], 2);
console.log(p2.items[0].id);
