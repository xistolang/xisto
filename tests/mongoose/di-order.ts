// tests/mongoose, part 4: the order Nest builds providers in. A service that asks for a forFeature model can be
// built before the model's provider ran: here a @Global() module exports it under an alias (useExisting) and a
// module imported before it needs the alias first (as an auth strategy of a shared module needs an app's resolver).
// It still gets the Model<X> of its schema, as on Node. No server is reached (lazyConnection: nothing runs a query).
import { Global, Inject, Injectable, Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { InjectModel, MongooseModule, MongooseModuleOptions } from '@nestjs/mongoose';
import { Model } from 'mongoose';
import { FleetEvent, FleetEventSchema, Rollout, RolloutSchema } from './schemas';

@Injectable()
export class RolloutRepo {
  constructor(@InjectModel(Rollout.name) private readonly model: Model<Rollout>) {}
  info(): string {
    return this.model.modelName + ' ' + this.model.collection.collectionName;
  }
}

@Injectable()
export class EventsRepo {
  constructor(@InjectModel(FleetEvent.name) private readonly events: Model<FleetEvent>) {}
  info(): string {
    return this.events.modelName + ' ' + this.events.collection.collectionName;
  }
}

export const EVENTS = 'EVENTS';

@Global()
@Module({
  imports: [MongooseModule.forFeature([{ name: Rollout.name, schema: RolloutSchema }, { name: FleetEvent.name, schema: FleetEventSchema }])],
  providers: [RolloutRepo, EventsRepo, { provide: EVENTS, useExisting: EventsRepo }],
  exports: [RolloutRepo, EventsRepo, EVENTS],
})
export class StoreModule {}

@Injectable()
export class Dashboard {
  constructor(
    @Inject(EVENTS) private readonly events: EventsRepo,
    private readonly rollouts: RolloutRepo,
  ) {}
  describe(): string {
    return this.events.info() + ' | ' + this.rollouts.info();
  }
}

@Module({ providers: [Dashboard], exports: [Dashboard] })
export class EarlyModule {}

@Module({
  imports: [MongooseModule.forRoot('mongodb://127.0.0.1:9/fleet_db', { lazyConnection: true } as MongooseModuleOptions), EarlyModule, StoreModule],
})
export class AppModule {}

async function main(): Promise<void> {
  const app = await NestFactory.create(AppModule, { logger: false });
  await app.init();
  const d = app.get(Dashboard) as Dashboard;
  console.log('dashboard', d.describe());
  console.log('same repo', (app.get(EVENTS) as EventsRepo) === (app.get(EventsRepo) as EventsRepo));
  process.exit(0);
}

main().catch((e: Error) => console.log('main failed', e.message));
