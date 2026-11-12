// tests/mongoose, part 3: with no server to reach (a closed port), as Node's mongoose 8.24 / @nestjs/mongoose 11:
// server selection fails after serverSelectionTimeoutMS with MongooseServerSelectionError and the driver's last
// error; the connection goes 2 -> 0 and emits 'disconnected' and 'error'; with bufferCommands: false an operation
// fails at once; a Nest forRoot app retries (retryAttempts) and then does not start.
import { Injectable, Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { MongooseModule } from '@nestjs/mongoose';
import { Connection, createConnection, Model } from 'mongoose';
import { Rollout, RolloutSchema } from './schemas';

@Injectable()
export class Probe {}

@Module({
  imports: [MongooseModule.forRoot('mongodb://127.0.0.1:1/fleet_db', { serverSelectionTimeoutMS: 200, retryAttempts: 2, retryDelay: 10 })],
  providers: [Probe],
})
export class EagerModule {}

async function main(): Promise<void> {
  const events: string[] = [];
  const conn: Connection = createConnection('mongodb://127.0.0.1:1/fleet_db?appName=offline', { serverSelectionTimeoutMS: 300 });
  console.log('connection', conn.readyState);
  conn.on('connected', () => events.push('connected'));
  conn.on('disconnected', () => events.push('disconnected'));
  conn.on('error', (e: unknown) => events.push('error ' + (e as Error).name + ': ' + (e as Error).message));
  const t0 = Date.now();
  try {
    await conn.asPromise();
    console.log('asPromise ok');
  } catch (e) {
    const err = e as Error;
    console.log('asPromise rejected', err.name, err.message, Date.now() - t0 >= 250);
  }
  await new Promise<void>((r) => setTimeout(r, 20));
  console.log('state after', conn.readyState, 'db', conn.db == null, 'events', events.join(' | '));

  const strict: Connection = createConnection('mongodb://127.0.0.1:1/fleet_db', { serverSelectionTimeoutMS: 100, bufferCommands: false });
  strict.on('error', () => {});
  const rollouts: Model<Rollout> = strict.model<Rollout>(Rollout.name, RolloutSchema);
  try {
    await rollouts.find({ status: 'ATIVO' }).lean().exec();
    console.log('find ok');
  } catch (e) {
    console.log('find rejected', (e as Error).name, (e as Error).message);
  }
  try {
    await rollouts.create({ nome: 'x' });
    console.log('create ok');
  } catch (e) {
    console.log('create rejected', (e as Error).name, (e as Error).message);
  }

  try {
    const app = await NestFactory.create(EagerModule, { logger: false, abortOnError: false });
    await app.init();
    console.log('forRoot app started');
  } catch (e) {
    console.log('forRoot app rejected', (e as Error).name, (e as Error).message);
  }
}

main().catch((e: Error) => console.log('main failed', e.message));
