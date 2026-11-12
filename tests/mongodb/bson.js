// tests/mongodb/bson.ts's reference: the same values through the bson package (js-bson 6) that the Node driver uses
const { serialize, deserialize, ObjectId, Decimal128, Long } = require('bson');
const docs = [
  { a: 1, b: 'x', c: 1.5, d: new Date(0), e: new ObjectId('0123456789abcdef01234567'), f: [true, null], g: { h: 2 ** 40 } },
  { i32max: 2147483647, past: 2147483648, i32min: -2147483648, below: -2147483649, big: 9007199254740991, frac: 0.1, e21: 1e21, neg: -7.25 },
  { s: 'acentuação ✓ "aspas" \\ \n\t \u0001 😀', empty: '', nested: { deeper: { list: [[1, 2], [], { k: 'v' }] } } },
  { dates: [new Date(-86400000), new Date(253402300799999), new Date(1762905600123)], bytes: Buffer.from('hello'), long: 1234567890123n },
  { $set: { 'a.b': 1 }, $in: ['x', 'y'], filter: { _id: { $in: [new ObjectId('aaaaaaaaaaaaaaaaaaaaaaaa'), new ObjectId('bbbbbbbbbbbbbbbbbbbbbbbb')] } } },
];
for (const d of docs) console.log(Buffer.from(serialize(d)).toString('hex'));
// decode: documents with int64, decimal128 and the rest, as the server sends them
const wire = [
  { n: Long.fromString('42'), big: Long.fromString('-9007199254740991'), dec: Decimal128.fromString('12.345'), dec2: Decimal128.fromString('-0.000001'), dec3: Decimal128.fromString('1E+10'), d: 3.0, o: new ObjectId('0123456789abcdef01234567'), t: new Date(1762905600123), u: [1, 'a', null, false, { x: [] }] },
  { s: 'acentuação ✓ "aspas" \\ \n\t \u0001 😀', when: new Date(-1) },
];
for (const w of wire) {
  const hex = Buffer.from(serialize(w)).toString('hex');
  console.log(hex);
  console.log(JSON.stringify(deserialize(Buffer.from(hex, 'hex'), { promoteLongs: true })));
}
