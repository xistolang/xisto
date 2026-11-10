interface TC<TEntity = number, K = string> { e: TEntity; k: K; }
const t: TC<number> = { e: 1, k: "a" };
const u: TC = { e: 2, k: "b" };
const v: TC<string, boolean> = { e: "x", k: true };
console.log(t.e, t.k, u.e, u.k, v.e, v.k);
