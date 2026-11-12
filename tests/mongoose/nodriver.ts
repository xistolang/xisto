// tests/mongoose, part 3 (xisto only: on Node these reach a server): without a MongoDB driver every operation
// rejects with NO_DRIVER, and so does opening a connection; the commands the queries would send (stage 2's input).
import { Injectable, Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { MongooseModule } from '@nestjs/mongoose';
import { createConnection, Model, NO_DRIVER, Types } from 'mongoose';
import { FleetEvent, FleetEventSchema, Rollout, RolloutSchema } from './schemas';

function short(e: Error): string {
  return e.message.startsWith(NO_DRIVER) ? 'NO_DRIVER' + e.message.substring(NO_DRIVER.length) : e.message;
}

async function attempt(label: string, f: () => Promise<string>): Promise<void> {
  try {
    console.log(label, 'ok', await f());
  } catch (e) {
    console.log(label, 'rejected:', short(e as Error));
  }
}

@Injectable()
export class Probe {}

@Module({ imports: [MongooseModule.forRoot('mongodb://user:secret@db.example:27017/fleet_db')], providers: [Probe] })
export class EagerModule {}

async function main(): Promise<void> {
  const conn = createConnection('mongodb://user:secret@db.example:27017/fleet_db?authSource=admin', { serverSelectionTimeoutMS: 500 });
  console.log('connection', conn.name, conn.host, conn.readyState);
  let errors = 0;
  conn.on('error', (e: unknown) => {
    errors++;
  });
  await attempt('asPromise', async () => {
    await conn.asPromise();
    return 'open';
  });
  console.log('state after', conn.readyState, 'error events', errors, 'db', conn.db === null);

  const rollouts: Model<Rollout> = conn.model<Rollout>(Rollout.name, RolloutSchema);
  const events: Model<FleetEvent> = conn.model<FleetEvent>('Event', FleetEventSchema, 'fleet_events');
  console.log('models', conn.modelNames().join(','), rollouts.collectionName, events.collectionName);

  const q = rollouts.find({ status: 'ATIVO', membros: { $in: ['T1', 'T2'] } }).sort({ createdAt: -1 }).skip(20).limit(10).select('nome status');
  console.log('find', JSON.stringify(q.toCommand()));
  console.log('count', JSON.stringify(rollouts.countDocuments({ nome: 'x' }).toCommand()));
  const id = new Types.ObjectId('0123456789abcdef01234567');
  console.log('update', JSON.stringify(rollouts.findOneAndUpdate({ _id: id }, { $set: { status: 'CONCLUIDO' } }, { new: true, upsert: true }).toCommand()));
  console.log('delete', JSON.stringify(events.deleteMany({ em: { $lt: new Date(0) } }).toCommand()));

  await attempt('await find', async () => {
    const docs = await rollouts.find({ status: 'ATIVO' }).lean();
    return String(docs.length);
  });
  await attempt('exec findOne', async () => {
    const doc = await rollouts.findOne({ nome: 'x' }).lean<Rollout | null>().exec();
    return doc === null ? 'null' : doc.nome;
  });
  await attempt('Promise.all', async () => {
    const [total, docs] = await Promise.all([rollouts.countDocuments({}), rollouts.find({}).limit(5).lean()]);
    return total + ' ' + docs.length;
  });
  await attempt('create', async () => {
    const d = await events.create({ nmMaquina: 'T1', em: new Date(0) });
    return d.nmMaquina;
  });
  let caught = '';
  void events.create({ nmMaquina: 'T2', em: new Date(0) }).catch((err: Error) => {
    caught = short(err);
  });
  await attempt('bulkWrite', async () => {
    const r = await events.bulkWrite([{ updateOne: { filter: { nmMaquina: 'T1' }, update: { $set: { em: new Date(0) } }, upsert: true } }], { ordered: false });
    return String(r.upsertedCount);
  });
  await new Promise<void>((r) => setTimeout(r, 10));
  console.log('create.catch', caught);
  await attempt('exists', async () => {
    const r = await rollouts.exists({ nome: 'x' });
    return r === null ? 'no' : String(r._id);
  });

  // a module with forRoot: the application does not start
  await attempt('forRoot app', async () => {
    const app = await NestFactory.create(EagerModule, { logger: false });
    await app.init();
    return 'started';
  });
}

main().catch((e: Error) => console.log('main failed', e.message));
