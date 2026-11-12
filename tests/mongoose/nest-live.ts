// tests/mongoose against a live MongoDB (XISTO_MONGO_URL; database xisto_nest_test): a NestJS app with the Mongo
// wiring of a real one, compared with Nest 11 + @nestjs/mongoose 11 + mongoose 8.24 on Node. A wrapper module's
// forRootAsync (useFactory from an injected config, bufferCommands: false, serverSelectionTimeoutMS, autoIndex and a
// connectionFactory that attaches a logger to the connection and asks the server who is connected), forFeature
// models in a feature module, a generic base service (upsert with findOneAndUpdate, a page with find + count in
// Promise.all), a health check that reads readyState, and app.close() closing the connection.
import { Controller, DynamicModule, Get, Injectable, Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { InjectConnection, InjectModel, MongooseModule, MongooseModuleFactoryOptions } from '@nestjs/mongoose';
import { Connection, ConnectionStates, FilterQuery, Model } from 'mongoose';
import { Notice, NoticeSchema, Profile, ProfileSchema } from './live-schemas';

const lines: string[] = [];

interface StatusReply {
  ok: number;
}

function attachLogger(connection: Connection, name: string): Connection {
  const report = (): void => {
    const hosts = connection.getClient().options.hosts.map((h) => String(h));
    lines.push('connected [' + name + '] hosts=' + hosts.join(',') + ' db=' + connection.name);
    const db = connection.db;
    if (db != null) {
      void db.command({ connectionStatus: 1 }).then((r) => {
        const s = r as StatusReply;
        lines.push('connectionStatus ok=' + String(s.ok));
      });
    }
  };
  if (connection.readyState === ConnectionStates.connected) {
    report();
  } else {
    connection.once('connected', report);
  }
  connection.on('disconnected', () => lines.push('disconnected [' + name + ']'));
  connection.on('error', (e: unknown) => lines.push('error ' + String(e)));
  return connection;
}

@Injectable()
export class MongoConfig {
  uri: string = process.env.XISTO_MONGO_URL ?? 'mongodb://127.0.0.1:27017';
  dbName: string = 'xisto_nest_test';
}

@Module({ providers: [MongoConfig], exports: [MongoConfig] })
export class ConfigModule {}

@Module({})
export class PlatformMongoModule {
  static forRootAsync(): DynamicModule {
    return {
      module: PlatformMongoModule,
      imports: [
        MongooseModule.forRootAsync({
          imports: [ConfigModule],
          inject: [MongoConfig],
          useFactory: async (cfg: MongoConfig): Promise<MongooseModuleFactoryOptions> => ({
            uri: cfg.uri,
            dbName: cfg.dbName,
            autoIndex: true,
            bufferCommands: false,
            serverSelectionTimeoutMS: 5000,
            connectionFactory: (connection: Connection, name: string): Connection => attachLogger(connection, name),
          }),
        }),
      ],
      exports: [MongooseModule],
    };
  }
}

export abstract class BaseMongoService<T> {
  protected constructor(protected readonly model: Model<T>) {}

  async upsert(filter: FilterQuery<T>, set: Record<string, unknown>): Promise<T> {
    const doc = await this.model.findOneAndUpdate(filter, { $set: set }, { upsert: true, new: true, setDefaultsOnInsert: true }).exec();
    return doc as T;
  }

  async page(filter: FilterQuery<T>, page: number, limit: number): Promise<{ items: T[]; total: number }> {
    const [items, total] = await Promise.all([
      this.model.find(filter).sort({ createdAt: -1, _id: -1 }).skip((page - 1) * limit).limit(limit).exec(),
      this.model.countDocuments(filter).exec(),
    ]);
    return { items: items, total: total };
  }
}

@Injectable()
export class ProfilesService extends BaseMongoService<Profile> {
  constructor(@InjectModel(Profile.name) model: Model<Profile>) {
    super(model);
  }
}

@Injectable()
export class NoticesService extends BaseMongoService<Notice> {
  constructor(@InjectModel(Notice.name) private readonly notices: Model<Notice>) {
    super(notices);
  }

  async seed(): Promise<number> {
    await this.notices.deleteMany({}).exec();
    for (let i = 1; i <= 5; i++) {
      await this.notices.create({ title: 'Aviso ' + String(i), views: i, tags: i % 2 === 0 ? ['par'] : [] });
    }
    return await this.notices.countDocuments({}).exec();
  }
}

@Injectable()
export class HealthService {
  constructor(@InjectConnection() private readonly mongo: Connection) {}

  check(): Record<string, string> {
    return { api: 'up', mongo: this.mongo.readyState === ConnectionStates.connected ? 'up' : 'down' };
  }
}

@Controller('health')
export class HealthController {
  constructor(private readonly health: HealthService) {}

  @Get()
  check(): Record<string, string> {
    return this.health.check();
  }
}

@Module({
  imports: [MongooseModule.forFeature([{ name: Notice.name, schema: NoticeSchema }, { name: Profile.name, schema: ProfileSchema }])],
  providers: [ProfilesService, NoticesService],
  exports: [ProfilesService, NoticesService],
})
export class PortalModule {}

@Module({ imports: [PlatformMongoModule.forRootAsync(), PortalModule], controllers: [HealthController], providers: [HealthService] })
export class AppModule {}

async function main(): Promise<void> {
  const app = await NestFactory.create(AppModule, { logger: false });
  await app.init();
  const health = app.get(HealthService) as HealthService;
  console.log('health', JSON.stringify(health.check()));
  const notices = app.get(NoticesService) as NoticesService;
  const profiles = app.get(ProfilesService) as ProfilesService;
  console.log('seeded', await notices.seed());
  const p1 = await notices.page({}, 1, 2);
  const p3 = await notices.page({ tags: 'par' }, 1, 10);
  console.log('page', p1.total, p1.items.map((n) => n.title).join(','), '|', p3.total, p3.items.map((n) => n.title).join(','));
  const pr = await profiles.upsert({ operador: 'OP9', base: 'BASE' }, { email: 'X@Y.COM' });
  const pr2 = await profiles.upsert({ operador: 'OP9', base: 'BASE' }, { classe: 5 });
  console.log('upsert', pr.operador, pr.email, pr2.classe, pr2.email);
  await new Promise<void>((r) => setTimeout(r, 50));
  await app.close();
  console.log('after close', JSON.stringify(health.check()));
  for (const l of lines) console.log(l);
}

main().catch((e: Error) => console.log('main failed', e.name, e.message));
