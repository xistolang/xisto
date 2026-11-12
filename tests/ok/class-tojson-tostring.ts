// toJSON(): string is what JSON.stringify writes for an object (bson's ObjectId: its hex digits), also inside
// objects, arrays, Records and unknown; String(x) and `${x}` call a class's toString(); new Ids.Oid(x) is the class
// the module exports (new Types.ObjectId(x) of mongoose reads the same way); a generic alias of a Record is a Record.
import { type Filter, Oid, Types } from './lib/mongo/ids.ts';
import * as Ids from './lib/mongo/ids.ts';

class Doc {
  id: Ids.Oid = new Ids.Oid('AB12');
  n: number = 1;
  other: Oid | null = null;
  tags: Oid[] = [new Oid('c3'), new Oid('D4')];
}

const d = new Doc();
const u: unknown = d;
const r: Record<string, unknown> = { a: new Oid('e5'), b: [new Oid('f6')] };
console.log(JSON.stringify(d));
console.log(JSON.stringify(u), JSON.stringify(r), JSON.stringify(new Oid('77')));
console.log(String(d.id), `id=${d.id}`, d.tags.map((t) => String(t)).join('+'), Types.label);
const q: Filter<Doc> = {};
q.name = 'x';
q.when = { $gte: 1 };
console.log(JSON.stringify(q), Object.keys(q).length);
