// symbol: Symbol("desc"), equal only to itself (Nest injection tokens)
const A = Symbol("TOKEN");
const B = Symbol("TOKEN");
const C: unique symbol = Symbol("other");
console.log(A === A, A === B, A.description, typeof A, String(C));
const tokens: symbol[] = [A, B, C];
console.log(tokens.length);
function name(t: unknown): string {
  if (typeof t === "symbol") return "symbol " + String(t as symbol);
  return "other";
}
console.log(name(A), name("x"));
const none = Symbol();
console.log(none.description == null);
