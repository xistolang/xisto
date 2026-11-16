// JSON.stringify's replacer and JSON.parse's reviver returning undefined: `return undefined` and a bare `return;`
// leave the member out (null in an array), in an arrow and in a function declared elsewhere, which still returns
// undefined when called directly. A replacer that adds to the Record it is given: only the keys there at the start
// are written, as in JavaScript.
function drop(key: string, value: any): any {
  if (key === "secret") return undefined;
  if (key === "gone") return;
  return value;
}
console.log(JSON.stringify({ a: 1, secret: "x", gone: 2, n: null }, drop));
console.log(drop("secret", 1) === undefined, drop("gone", 1) === undefined, drop("a", 5));
console.log(JSON.stringify([1, "secret", 3], (k: string, v: any) => (v === "secret" ? undefined : v)));
console.log(JSON.stringify({ x: 1, y: 2 }, (k: string, v: any) => {
  if (k === "x") return;
  return v;
}));
const rev: any = JSON.parse("[1, 2, 3]", (k: string, v: any) => (v === 2 ? undefined : v));
console.log(rev.length, JSON.stringify(rev)); // (Node: a hole at 1, which xisto holds as null)
const obj: any = JSON.parse('{"a":1,"b":2}', (k: string, v: any) => (v === 2 ? undefined : v));
console.log(obj);
const rec: Record<string, number> = { a: 1, b: 2, c: 3 };
let i = 0;
console.log(JSON.stringify(rec, (k: string, v: any) => {
  if (k === "a") for (let j = 0; j < 50; j++) rec["k" + j] = j;
  i++;
  return v;
}));
console.log(i);
