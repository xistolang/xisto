// MongooseModule, SchemaFactory, tokens and injection of @nestjs/mongoose (v10/11) for xisto, over the mongoose package (stage 1: no MongoDB driver yet).
//
// @Schema(options) and @Prop(options) record a class's paths; SchemaFactory.createForClass(C) makes the mongoose
// Schema from them (a @Prop without `type` takes it from the field's TypeScript type, as Nest does from
// design:type). MongooseModule.forRoot/forRootAsync provide a Connection under getConnectionToken(name);
// forFeature/forFeatureAsync a Model under getModelToken(name, connection), which @InjectModel(name) and
// @InjectConnection() ask for.
//
// Nothing happens until a module is imported: an application that does not import MongooseModule (a Mongo that is
// optional) gets null in an @Optional() @InjectModel(X.name) parameter, as Nest does. A forRoot connection is
// opened when the application starts; without a driver that fails the start with an Error that begins with
// "mongoose: no MongoDB driver in xisto yet (stage 2)" (Nest would retry 9 times first; there is nothing to retry).
// Node: a Model<X> constructor parameter is made from the model's state (its names) as the exact Model<X> it
// declares; a model a provider of the application builds (connection.model(...) in a useFactory) reaches a
// Model<X> parameter only once the driver exists (stage 2).
import { ClassInfo, MethodInfo } from "xisto:reflect";
import { DynamicModule, FactoryProvider, Inject, Module, Provider, __jsonValues } from "@nestjs/common";
import { Connection, ConnectOptions, ObjectIdStatic, Schema, SchemaTypeOptions, SchemaTypes, createConnection, __modelState, __registerConnection } from "mongoose";
import { PropDef, __propsByClass, __schemaOptionsByClass } from "./decorators";

export const DEFAULT_DB_CONNECTION = "DatabaseConnection";

// ---------- tokens ----------

/** the token of a connection: "DatabaseConnection" for the default one, "<name>Connection" for another */
export function getConnectionToken(name: string | null = null): string {
  return name !== null && name.length > 0 && name !== DEFAULT_DB_CONNECTION ? name + "Connection" : DEFAULT_DB_CONNECTION;
}

/** the token of a model: "<Name>Model" on the default connection, "<name>Connection/<Name>Model" on another */
export function getModelToken(model: string, connectionName: string | null = null): string {
  if (connectionName === null) {
    return model + "Model";
  }
  return getConnectionToken(connectionName) + "/" + model + "Model";
}

function connKey(connectionName: string): string {
  return "nest:" + getConnectionToken(connectionName.length > 0 ? connectionName : null);
}

function collectProps(c: ClassInfo, out: PropDef[]): void {
  const base = c.base;
  if (base !== null) {
    collectProps(base, out);
  }
  const own = __propsByClass.get(c.id);
  if (own !== null) {
    for (const p of own) {
      out.push(p);
    }
  }
}

// Nest reads a `type:` that is a class other than String/Number/Boolean/Date/Map/Buffer (and not a mongoose
// SchemaType) as a nested @Schema class: Types.ObjectId (bson's class) has no @Prop, so the path is Mixed, as on Node
// (Schema.Types.ObjectId / SchemaTypes.ObjectId give an ObjectId path)
function nestType(o: SchemaTypeOptions): SchemaTypeOptions {
  const t = o.type ?? null;
  if (t instanceof ObjectIdStatic) {
    return { ...o, type: SchemaTypes.Mixed };
  }
  if (Array.isArray(t)) {
    const a = t as unknown[];
    if (a.length > 0 && a[0] instanceof ObjectIdStatic) {
      return { ...o, type: [SchemaTypes.Mixed] };
    }
  }
  return o;
}

export class SchemaFactory {
  /** the mongoose Schema of a @Schema() class: its @Prop paths (a base class's first) and its options */
  static createForClass(target: ClassInfo): Schema {
    const props: PropDef[] = [];
    collectProps(target, props);
    const def: Record<string, SchemaTypeOptions> = {};
    for (const p of props) {
      def[p.name] = nestType(p.options);
    }
    return new Schema(def, __schemaOptionsByClass.get(target.id));
  }
}

// ---------- MongooseModule ----------

/** forFeature's entries: { name: User.name, schema: UserSchema, collection? } */
export interface ModelDefinition {
  name: string;
  schema: Schema;
  collection?: string;
}

/** forFeatureAsync's entries: the schema made by a factory */
export interface AsyncModelFactory {
  name: string;
  collection?: string;
  imports?: unknown[];
  inject?: unknown[];
  useFactory: (...args: any[]) => Schema;
}

/** forRoot's options: a connection's, plus Nest's own */
export interface MongooseModuleOptions extends ConnectOptions {
  uri?: string;
  retryAttempts?: number;
  retryDelay?: number;
  connectionName?: string;
  connectionFactory?: (connection: Connection, name: string) => Connection;
  lazyConnection?: boolean;
  onConnectionCreate?: (connection: Connection) => void;
  verboseRetryLog?: boolean;
}

/** what forRootAsync's factory returns */
export type MongooseModuleFactoryOptions = Omit<MongooseModuleOptions, "connectionName">;

export interface MongooseModuleAsyncOptions {
  connectionName?: string;
  imports?: unknown[];
  inject?: unknown[];
  useFactory?: (...args: any[]) => Promise<MongooseModuleFactoryOptions> | MongooseModuleFactoryOptions;
}

function connectOptions(o: MongooseModuleFactoryOptions): ConnectOptions {
  return {
    dbName: o.dbName,
    user: o.user,
    pass: o.pass,
    authSource: o.authSource,
    autoIndex: o.autoIndex,
    autoCreate: o.autoCreate,
    bufferCommands: o.bufferCommands,
    serverSelectionTimeoutMS: o.serverSelectionTimeoutMS,
    connectTimeoutMS: o.connectTimeoutMS,
    socketTimeoutMS: o.socketTimeoutMS,
    maxPoolSize: o.maxPoolSize,
    minPoolSize: o.minPoolSize,
    appName: o.appName,
    tls: o.tls,
    replicaSet: o.replicaSet,
    directConnection: o.directConnection,
    family: o.family,
  };
}

// opens the connection as Nest does (createConnection, onConnectionCreate, asPromise unless lazy, connectionFactory)
async function openConnection(uri: string, o: MongooseModuleFactoryOptions, connectionName: string): Promise<Connection> {
  const conn = createConnection(uri, connectOptions(o));
  __registerConnection(connKey(connectionName), conn);
  if (o.lazyConnection !== true) {
    const onCreate = o.onConnectionCreate;
    if (onCreate != null) {
      onCreate(conn);
    }
    await conn.asPromise();
  }
  const factory = o.connectionFactory;
  if (factory != null) {
    return factory(conn, connectionName.length > 0 ? connectionName : DEFAULT_DB_CONNECTION);
  }
  return conn;
}

@Module({})
export class MongooseModule {
  /** MongooseModule.forRoot(uri, options): the connection, opened when the application starts */
  static forRoot(uri: string, options: MongooseModuleOptions = {}): DynamicModule {
    const name = options.connectionName ?? "";
    const fp: FactoryProvider = {
      provide: getConnectionToken(name),
      useFactory: async (): Promise<unknown> => await openConnection(uri, options, name),
    };
    const providers: Provider[] = [fp];
    return { module: MongooseModule, providers: providers, exports: [getConnectionToken(name)], global: true };
  }

  /** MongooseModule.forRootAsync({ useFactory, inject }): the same, with the options from a factory */
  static forRootAsync(options: MongooseModuleAsyncOptions): DynamicModule {
    const name = options.connectionName ?? "";
    const make = options.useFactory;
    const fp: FactoryProvider = {
      provide: getConnectionToken(name),
      inject: options.inject ?? [],
      useFactory: async (...args: any[]): Promise<unknown> => {
        if (make == null) {
          throw new Error("MongooseModule.forRootAsync: useFactory is required (useClass/useExisting are not supported)");
        }
        const o = await make(...args);
        return await openConnection(o.uri ?? "", o, name);
      },
    };
    const providers: Provider[] = [fp];
    return { module: MongooseModule, imports: options.imports ?? [], providers: providers, exports: [getConnectionToken(name)], global: true };
  }

  /** MongooseModule.forFeature([{ name: User.name, schema: UserSchema }]): a model for each, for @InjectModel */
  static forFeature(models: ModelDefinition[] = [], connectionName: string = ""): DynamicModule {
    const providers: Provider[] = [];
    const tokens: unknown[] = [];
    for (const m of models) {
      const token = getModelToken(m.name, connectionName.length > 0 ? connectionName : null);
      const def = m;
      const fp: FactoryProvider = {
        provide: token,
        inject: [getConnectionToken(connectionName)],
        useFactory: (conn: Connection): unknown => {
          // a Model<X> constructor parameter is decoded from this state (once the module is in the application:
          // the providers are built in order, the modules' imports first)
          __jsonValues.set(token, __modelState(connKey(connectionName), def.name, def.schema, def.collection ?? ""));
          return conn.model(def.name, def.schema, def.collection ?? "");
        },
      };
      providers.push(fp);
      tokens.push(token);
    }
    return { module: MongooseModule, providers: providers, exports: tokens };
  }

  /** MongooseModule.forFeatureAsync([{ name, useFactory: () => schema }]): the schemas made at import */
  static forFeatureAsync(factories: AsyncModelFactory[] = [], connectionName: string = ""): DynamicModule {
    const models: ModelDefinition[] = [];
    for (const f of factories) {
      models.push({ name: f.name, schema: f.useFactory(), collection: f.collection });
    }
    return MongooseModule.forFeature(models, connectionName);
  }
}

// ---------- injection ----------

/** @InjectModel(User.name) private readonly users: Model<User> */
export function InjectModel(model: string, connectionName: string = ""): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return Inject(getModelToken(model, connectionName.length > 0 ? connectionName : null));
}

/** @InjectConnection() private readonly connection: Connection */
export function InjectConnection(name: string = ""): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return Inject(getConnectionToken(name));
}
