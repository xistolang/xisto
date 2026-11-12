// mongoose + @nestjs/mongoose without a database (stage 1), against the same program on Node with mongoose 8.24 and
// @nestjs/mongoose 11.0 (expected.txt): schemas made from @Schema/@Prop classes (paths, options, indexes, clone),
// ObjectId (hex, JSON, timestamp, equals, isValid, generated ids), tokens, and Nest's injection: a model and a
// connection that are @Optional() are null when the application does not import the Mongo modules (a Mongo that is
// optional, as an app that registers them only with MONGO_URI), and a forFeature model is the model of its schema.
import { Injectable, Module, Optional } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { getConnectionToken, getModelToken, InjectConnection, InjectModel } from '@nestjs/mongoose';
import { Connection, ConnectionStates, isValidObjectId, Model, Schema, Types } from 'mongoose';
import { FleetEventSchema, Rollout, RolloutDocument, RolloutSchema, SettingsSchema } from './schemas';

@Injectable()
export class RolloutService {
  constructor(
    @Optional()
    @InjectModel(Rollout.name)
    private readonly model?: Model<RolloutDocument>,
    @Optional() @InjectConnection() private readonly conn?: Connection,
  ) {}

  get isEnabled(): boolean {
    return !!this.model;
  }

  mongoState(): string {
    if (!this.conn) return 'disabled';
    return this.conn.readyState === ConnectionStates.connected ? 'up' : 'down';
  }

  async listPage(page: number, pageSize: number): Promise<{ total: number; items: string[] }> {
    if (!this.model) return { total: 0, items: [] };
    const filter = { status: 'ATIVO' };
    const [total, docs] = await Promise.all([
      this.model.countDocuments(filter),
      this.model
        .find(filter)
        .sort({ createdAt: -1 })
        .skip((page - 1) * pageSize)
        .limit(pageSize)
        .lean(),
    ]);
    return { total, items: docs.map((d) => d.nome) };
  }

  async byName(nome: string): Promise<Rollout | null> {
    if (!this.model) return null;
    return this.model.findOne({ nome }).lean<Rollout | null>().exec();
  }

  modelInfo(): string {
    if (!this.model) return 'no model';
    return this.model.modelName + ' ' + this.model.collection.collectionName;
  }
}

@Module({ providers: [RolloutService] })
export class NoMongoModule {}

function show(label: string, v: unknown): void {
  console.log(label, JSON.stringify(v));
}

function report(name: string, s: Schema): void {
  console.log('schema', name, Object.keys(s.paths).join(','));
  for (const k of Object.keys(s.paths)) {
    const p = s.path(k);
    if (!p) continue;
    const enums = p.options.enum ? JSON.stringify(p.options.enum) : '-';
    const elem = s.path(k + '.$');
    console.log(' ', k, p.instance, p.isRequired === true, enums, elem ? elem.instance : '-');
  }
  show('  indexes', s.indexes());
  show('  options', [s.get('collection'), s.get('timestamps'), s.get('_id'), s.get('autoIndex')]);
}

async function main(): Promise<void> {
  // schemas
  report('Settings', SettingsSchema);
  report('Rollout', RolloutSchema);
  report('FleetEvent', FleetEventSchema);
  const copy = RolloutSchema.clone().set('autoIndex', false);
  show('clone autoIndex', [copy.get('autoIndex'), RolloutSchema.get('autoIndex'), Object.keys(copy.paths).length]);

  // ObjectId
  const id = new Types.ObjectId('0123456789abcdef01234567');
  console.log('oid', id.toHexString(), id.toString(), String(id), `${id}`);
  show('oid json', { id, list: [id] });
  console.log('oid time', id.getTimestamp().toISOString());
  console.log('oid equals', id.equals('0123456789ABCDEF01234567'), id.equals(new Types.ObjectId('0123456789abcdef01234567')), id.equals('0123456789abcdef01234568'));
  for (const v of ['0123456789abcdef01234567', '0123456789ABCDEF01234567', 'xyz', '', '123456789012', 'zzzzzzzzzzzzzzzzzzzzzzzz']) {
    console.log('isValid', JSON.stringify(v), Types.ObjectId.isValid(v), isValidObjectId(v));
  }
  try {
    new Types.ObjectId('xyz');
  } catch (e) {
    console.log('invalid', (e as Error).name, (e as Error).message);
  }
  console.log('fromTime', Types.ObjectId.createFromTime(1700000000).toHexString(), Types.ObjectId.createFromHexString('00000000000000000000abcd').toHexString());
  const a = new Types.ObjectId();
  const b = new Types.ObjectId();
  const now = Math.floor(Date.now() / 1000);
  const secs = Math.floor(a.getTimestamp().getTime() / 1000);
  console.log('generated', a.toHexString().length, a.toHexString().slice(8, 18) === b.toHexString().slice(8, 18),
    parseInt(b.toHexString().slice(18), 16) - parseInt(a.toHexString().slice(18), 16), Math.abs(now - secs) <= 5, Types.ObjectId.isValid(a.toHexString()));

  // tokens
  console.log('tokens', getModelToken('Rollout'), getModelToken('Rollout', 'portal'), getConnectionToken(), getConnectionToken('portal'));

  // an application without the Mongo modules: the @Optional() model and connection are null
  const app = await NestFactory.create(NoMongoModule, { logger: false });
  await app.init();
  const svc = app.get(RolloutService) as RolloutService;
  console.log('no mongo', svc.isEnabled, svc.mongoState(), svc.modelInfo());
  show('no mongo list', await svc.listPage(1, 10));
  show('no mongo byName', await svc.byName('x'));
  await app.close();

}

main().catch((e: Error) => console.log('main failed', e.message));
