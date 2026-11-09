// object[] (values of any shape, kept as JSON text) inside a value written with JSON.stringify and read back with
// JSON.parse: the compiler used to crash making the reader of an object element.
interface Batch {
  name: string;
  ops: object[];
}
const ops: object[] = [];
for (let i: int = 0; i < 3; i++) {
  ops.push({ index: { _index: "orders", _id: `k${i}` } });
  ops.push({ company: `Company ${i}`, amount: Number(i) * 1.5, tags: ["a", "b"], ok: i % 2 === 0 });
}
const b: Batch = { name: "bulk", ops: ops };
const text = JSON.stringify(b);
console.log(text);
const back: Batch = JSON.parse(text);
console.log(back.name, back.ops.length, JSON.stringify(back.ops[1]), JSON.stringify(back.ops[4]));
const nested: object = JSON.parse('{"a":[1,"x",null,true,{"b":"\\u00e7\\n"}],"c":-0.5}');
console.log(JSON.stringify(nested));
