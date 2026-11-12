// packages/mongodb's BSON, with no server: the values a program writes, as the bytes js-bson 6 makes of them
// (expected-bson.txt is bson.js on Node), and documents as the server sends them (int64, decimal128, dates
// before 1970, UTF-8) read back as JSON as Node's JSON.stringify of the deserialized document.
import { ObjectId, deserializeHexToJson, serializeHex } from 'mongodb';

const docs: unknown[] = [
  { a: 1, b: 'x', c: 1.5, d: new Date(0), e: new ObjectId('0123456789abcdef01234567'), f: [true, null], g: { h: 2 ** 40 } },
  { i32max: 2147483647, past: 2147483648, i32min: -2147483648, below: -2147483649, big: 9007199254740991, frac: 0.1, e21: 1e21, neg: -7.25 },
  { s: 'acentuação ✓ "aspas" \\ \n\t \u0001 😀', empty: '', nested: { deeper: { list: [[1, 2], [], { k: 'v' }] } } },
  { dates: [new Date(-86400000), new Date(253402300799999), new Date(1762905600123)], bytes: Buffer.from('hello'), long: 1234567890123n },
  { $set: { 'a.b': 1 }, $in: ['x', 'y'], filter: { _id: { $in: [new ObjectId('aaaaaaaaaaaaaaaaaaaaaaaa'), new ObjectId('bbbbbbbbbbbbbbbbbbbbbbbb')] } } },
];
for (const d of docs) console.log(serializeHex(d));
// documents as bson.js wrote them on Node
const wire = [
  'ae000000126e002a000000000000001262696700010000000000e0ff136465630039300000000000000000000000003a30136465633200010000000000000000000000000034b01364656333000100000000000000000000000000543010640003000000076f000123456789abcdef012345670974007bb05c759a0100000475002c000000103000010000000231000200000061000a3200083300000334000d0000000478000500000000000000',
  '3f000000027300250000006163656e747561c3a7c3a36f20e29c932022617370617322205c200a09200120f09f988000097768656e00ffffffffffffffff00',
];
for (const hex of wire) {
  console.log(hex);
  console.log(deserializeHexToJson(hex));
}
