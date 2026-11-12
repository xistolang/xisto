// tests/mongoose, part 2: the Mongo modules imported (MongooseModule.forRoot with a lazy connection, forFeature):
// @InjectModel gives the model of its schema, the connection and the models are providers. No server is reached
// (nothing runs a query). A separate program: Nest builds one application per process here.
import { Injectable, Module, Optional } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { getConnectionToken, getModelToken, InjectConnection, InjectModel, MongooseModule, MongooseModuleOptions } from '@nestjs/mongoose';
import { Connection, ConnectionStates, FilterQuery, Model } from 'mongoose';
import { FleetEvent, FleetEventSchema, Rollout, RolloutDocument, RolloutSchema } from './schemas';

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

  modelInfo(): string {
    if (!this.model) return 'no model';
    return this.model.modelName + ' ' + this.model.collection.collectionName;
  }
}

@Injectable()
export class EventsService {
  constructor(@InjectModel(FleetEvent.name) private readonly events: Model<FleetEvent>) {}

  async count(nmMaquina: string): Promise<number> {
    const query: FilterQuery<FleetEvent> = {};
    query.nmMaquina = nmMaquina;
    query.em = { $gte: new Date(0) };
    return this.events.countDocuments(query).exec();
  }

  info(): string {
    return this.events.modelName + ' ' + this.events.collection.collectionName;
  }
}

// the Mongo modules imported, the connection lazy (no server is reached while nothing runs a query)
@Module({
  imports: [
    MongooseModule.forRoot('mongodb://127.0.0.1:9/fleet_db', { lazyConnection: true, serverSelectionTimeoutMS: 100 } as MongooseModuleOptions),
    MongooseModule.forFeature([
      { name: Rollout.name, schema: RolloutSchema },
      { name: FleetEvent.name, schema: FleetEventSchema },
    ]),
  ],
  providers: [RolloutService, EventsService],
})
export class WithMongoModule {}

async function main(): Promise<void> {
  const app = await NestFactory.create(WithMongoModule, { logger: false });
  await app.init();
  const svc2 = app.get(RolloutService) as RolloutService;
  const ev = app.get(EventsService) as EventsService;
  console.log('with mongo', svc2.isEnabled, svc2.mongoState(), svc2.modelInfo(), ev.info());
  const conn = app.get(getConnectionToken()) as Connection;
  console.log('connection', conn.readyState === ConnectionStates.connected);
  // (xisto: the provider under getModelToken() is a Model<unknown>; `as Model<FleetEvent>` of it needs stage 2)
  console.log('model token', getModelToken(FleetEvent.name));
  process.exit(0); // (Node: before the lazy connection's background attempt fails)
}

main().catch((e: Error) => console.log('main failed', e.message));
