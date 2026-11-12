// tests/mongoose, against a live MongoDB (XISTO_MONGO_URL; database xisto_live_test, dropped at the start): the
// operations a NestJS app does with mongoose 8, compared with Node's mongoose 8.24 on the same server. ObjectIds
// and the times of "now" are masked; what was written is read back raw (key order included).
import { createConnection, Connection, Model, Types } from 'mongoose';
import { Notice, NoticeRead, NoticeReadSchema, NoticeSchema, Profile, ProfileSchema } from './live-schemas';

const ids: string[] = [];
function mask(s: string): string {
  let out = s.replace(/[0-9a-f]{24}/g, (h: string) => {
    let i = ids.indexOf(h);
    if (i < 0) {
      ids.push(h);
      i = ids.length - 1;
    }
    return 'id' + String(i);
  });
  out = out.replace(/20[2-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9:.]+Z/g, 'NOW');
  return out;
}
function show(label: string, v: unknown): void {
  console.log(label, mask(JSON.stringify(v)));
}

interface NoticeLean {
  _id: Types.ObjectId;
  title: string;
  kind: string;
  views: number;
}
interface IndexInfo {
  name: string;
  unique?: boolean;
  sparse?: boolean;
}
interface IndexList {
  cursor: { firstBatch: IndexInfo[] };
}
interface Bucket {
  _id: string;
  count: number;
}

// (sorted: the fields an upsert takes from its filter come in an order the server chooses, which varies)
async function raw(conn: Connection, coll: string, sorted: boolean = false): Promise<void> {
  const db = conn.db;
  if (db == null) return;
  const docs = await db.collection(coll).find({}).sort({ _id: 1 }).toArray();
  for (const d of docs) {
    if (!sorted) {
      show('  raw ' + coll, d);
      continue;
    }
    const out: Record<string, unknown> = {};
    for (const k of Object.keys(d).sort()) out[k] = d[k];
    show('  raw ' + coll + ' (sorted)', out);
  }
}

async function main(): Promise<void> {
  const url = process.env.XISTO_MONGO_URL ?? 'mongodb://127.0.0.1:27017';
  const conn = createConnection(url, { dbName: 'xisto_live_test', serverSelectionTimeoutMS: 3000, autoIndex: true });
  const events: string[] = [];
  conn.on('connected', () => events.push('connected'));
  conn.on('disconnected', () => events.push('disconnected'));
  await conn.asPromise();
  console.log('open', conn.readyState, conn.name, events.join(','));
  await conn.db!.dropDatabase();

  const notices: Model<Notice> = conn.model<Notice>(Notice.name, NoticeSchema);
  const profiles: Model<Profile> = conn.model<Profile>(Profile.name, ProfileSchema);
  const reads: Model<NoticeRead> = conn.model<NoticeRead>(NoticeRead.name, NoticeReadSchema);
  await notices.init();
  await profiles.init();
  await reads.init();
  const ix = (await conn.db!.command({ listIndexes: 'profiles' })) as IndexList;
  console.log('indexes', ix.cursor.firstBatch.map((i) => i.name + (i.unique === true ? '!' : '') + (i.sparse === true ? '?' : '')).join(' '));

  const a = await notices.create({ title: 'Primeiro', tags: ['a', 'b'], target: { operador: 'OP1', base: 'BASE' } });
  console.log('create', a.title, a.kind, a.views, a.tags.join('+'), a.target?.operador, a.createdAt instanceof Date);
  const many = await notices.insertMany([{ title: 'Segundo', kind: 'warning', views: 3 }, { title: 'Terceiro', views: 7, expiresAt: new Date(86400000) }]);
  console.log('insertMany', many.length, many[1].title, many[1].expiresAt?.toISOString());
  await raw(conn, 'notices');

  const page = await notices.find({ views: { $gte: 0 } }).sort({ views: -1 }).skip(1).limit(2).lean<NoticeLean[]>().exec();
  console.log('page', page.map((n) => n.title + ':' + String(n.views)).join(' '));
  const [items, total] = await Promise.all([notices.find({}).sort({ title: 1 }).exec(), notices.countDocuments({}).exec()]);
  console.log('all', total, items.map((n) => n.title).join(','));
  const one = await notices.findOne({ title: 'Segundo' }).exec();
  console.log('findOne', one?.kind, one?.views);
  const byId = await notices.findById(many[0]._id?.toHexString() ?? '').lean<NoticeLean | null>().exec();
  console.log('findById', byId?.title);
  console.log('none', await notices.findOne({ title: 'nada' }).exec());
  console.log('exists', (await notices.exists({ title: 'Primeiro' })) !== null, await notices.exists({ title: 'nada' }));
  show('distinct', await notices.distinct('kind'));
  console.log('count', await notices.countDocuments({ kind: 'info' }), await notices.countDocuments({ 'target.operador': 'OP1' }));

  const up = await profiles.findOneAndUpdate({ operador: 'OP1', base: 'BASE' }, { $set: { email: '  Ana@X.COM ', classe: 2 } }, { upsert: true, new: true }).exec();
  console.log('upsert', up?.operador, up?.email, up?.classe);
  const again = await profiles.findOneAndUpdate({ operador: 'OP1', base: 'BASE' }, { classe: 3 }, { new: true }).exec();
  console.log('update', again?.classe);
  const old = await profiles.findOneAndUpdate({ operador: 'OP1', base: 'BASE' }, { $set: { classe: 4 } }).exec();
  console.log('old', old?.classe);
  await raw(conn, 'profiles', true);
  show('updateOne', await profiles.updateOne({ operador: 'OP2', base: 'X' }, { $set: { classe: 1 } }, { upsert: true }).exec());
  show('updateMany', await profiles.updateMany({}, { $inc: { classe: 10 } }).exec());
  try {
    await profiles.create({ operador: 'OP1', base: 'BASE' });
  } catch (e) {
    const err = e as Error;
    console.log('duplicate', err.name, mask(err.message));
  }

  const r = await reads.create({ noticeId: a._id, operador: 'OP1' });
  console.log('read', r.operador, r.readAt instanceof Date);
  await raw(conn, 'reads');

  const buckets = await notices.aggregate<Bucket>([{ $group: { _id: '$kind', count: { $sum: 1 } } }, { $sort: { _id: 1 } }]).exec();
  console.log('aggregate', buckets.map((b) => b._id + '=' + String(b.count)).join(' '));
  const bw = await notices.bulkWrite([
    { updateOne: { filter: { title: 'Quarto' }, update: { $set: { views: 1 } }, upsert: true } },
    { insertOne: { document: { title: 'Quinto' } } },
    { deleteMany: { filter: { title: 'Segundo' } } },
  ]);
  console.log('bulkWrite', bw.insertedCount, bw.upsertedCount, bw.deletedCount);
  show('deleteOne', await notices.deleteOne({ title: 'Quinto' }).exec());
  show('deleteMany', await notices.deleteMany({ views: { $lt: 5 } }).exec());
  const gone = await notices.findOneAndDelete({ title: 'Terceiro' }).exec();
  console.log('findOneAndDelete', gone?.title, await notices.countDocuments({}).exec());

  for (let i = 0; i < 230; i++) {
    await reads.create({ noticeId: new Types.ObjectId(), operador: 'B' + String(i) });
  }
  const big = await reads.find({ operador: { $regex: '^B' } }).lean().exec();
  console.log('batches', big.length);

  await conn.db!.dropDatabase();
  await conn.close();
  console.log('closed', conn.readyState, events.join(','));
}

main().catch((e: Error) => console.log('main failed', e.name, e.message));
