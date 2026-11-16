// a const symbol as a key: { [T]: v }, o[T] (a field of its own, shown after the string keys as Node shows it, left out
// of JSON.stringify and Object.keys); Map<symbol, V>; sym.toString(); a symbol as a DI token's value
const T = Symbol("t");
const N = Symbol();
const S: unique symbol = Symbol("a b");
const o = { [T]: 1, a: 2, [N]: "x", [S]: [1] };
console.log(o);
console.log({ z: { [T]: 1 } });
console.log(Object.keys(o), JSON.stringify(o), Object.entries(o));
const p = { ...o, b: 1 };
console.log(p, p[T]);
o[T] = 5;
o[T] += 1;
console.log(o[T], o[S].length);
function inner(): void {
  const q = { n: "a", [T]: true };
  console.log(q, q[T]);
}
inner();
const m = new Map<symbol, number>();
console.log(m);
m.set(T, 5);
m.set(N, 6);
m.set(T, 7);
console.log(m.get(T), m.get(N), m.has(Symbol("t")), m.size, m);
console.log(m.delete(N), m.size, m);
console.log(T.toString(), String(N), T.description);
const TOKEN = Symbol("TOKEN");
const providers = [{ provide: TOKEN, useValue: 42 }];
console.log(providers[0].provide === TOKEN, providers);
