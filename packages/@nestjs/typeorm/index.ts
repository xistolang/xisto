// @nestjs/typeorm for xisto: TypeOrmModule.forRoot / forRootAsync make the DataSource, forFeature the repositories of
// some entities, and @InjectRepository(Entity) / @InjectDataSource() hand them to a constructor, as in Nest.
//
// Nest's dynamic modules become plain modules here: forRoot and forFeature register what they provide (the
// DataSource by its type, each Repository<Entity> by its token) and return a module class to put in `imports`.
// A Repository<Entity> is made from its parameter's own type (its state is the DataSource's name), so the
// constructor gets the exact Repository<User> it declares.
import { ClassInfo, MethodInfo } from "xisto:reflect";
import { FactoryProvider, Module, __appFactories, __jsonValues, __tokens, __values } from "@nestjs/common";
import { DataSource, DataSourceOptions } from "typeorm";

/** forRoot's options: a DataSource's, plus Nest's own (autoLoadEntities, retryAttempts... are accepted) */
export interface TypeOrmModuleOptions extends DataSourceOptions {
  autoLoadEntities?: boolean;
  retryAttempts?: int;
  retryDelay?: int;
  keepConnectionAlive?: boolean;
}

/** forRootAsync's options: a factory that returns the options (at import; with inject, when the application starts) */
export interface TypeOrmModuleAsyncOptions {
  name?: string;
  imports?: ClassInfo[];
  inject?: unknown[];
  useFactory: (...args: any[]) => TypeOrmModuleOptions;
}

@Module({})
class TypeOrmCoreModule {}

@Module({})
class TypeOrmFeatureModule {}

/** the token of an entity's repository: what forFeature registers and @InjectRepository asks for */
export function getRepositoryToken(entity: ClassInfo, dataSource: string = "default"): string {
  return "Repository<" + entity.id + ">@" + dataSource;
}

/** the token of a DataSource by name ("default": the DataSource type itself) */
export function getDataSourceToken(dataSource: string = "default"): string {
  return dataSource === "default" ? __reflect_typeid(DataSource) : "DataSource@" + dataSource;
}

function dataSourceOptions(o: TypeOrmModuleOptions): DataSourceOptions {
  return {
    type: o.type,
    host: o.host,
    port: o.port,
    username: o.username,
    password: o.password,
    database: o.database,
    schema: o.schema,
    connectString: o.connectString,
    sid: o.sid,
    serviceName: o.serviceName,
    entities: o.entities,
    synchronize: o.synchronize,
    logging: o.logging,
    poolSize: o.poolSize,
    name: o.name,
  };
}

function provideDataSource(options: TypeOrmModuleOptions): DataSource {
  const ds = new DataSource(dataSourceOptions(options)); // (it registers itself under its name and connects on its first query)
  __values.set(getDataSourceToken(options.name ?? "default"), ds);
  return ds;
}

/** a module class too: `exports: [TypeOrmModule]` re-exports what forRoot / forFeature registered (they are global here) */
@Module({})
export class TypeOrmModule {
  /** TypeOrmModule.forRoot({ type: "oracle", ... }): the DataSource the application's repositories use */
  static forRoot(options: TypeOrmModuleOptions): ClassInfo {
    provideDataSource(options);
    return TypeOrmCoreModule;
  }

  /** TypeOrmModule.forRootAsync({ useFactory: () => ({ ... }) }): the same, with options computed at import */
  static forRootAsync(options: TypeOrmModuleAsyncOptions): ClassInfo {
    const name = options.name ?? "";
    const inject = options.inject ?? [];
    if (inject.length > 0) {
      // the factory needs providers (inject: [ConfigService]): it runs when the application starts, as in Nest
      const fp: FactoryProvider = {
        provide: getDataSourceToken(name.length > 0 ? name : "default"),
        inject: inject,
        useFactory: (...args: any[]): unknown => {
          const made = options.useFactory(...args);
          if (name.length > 0 && (made.name ?? "").length === 0) {
            made.name = name;
          }
          return provideDataSource(made);
        },
      };
      __appFactories.push(fp);
      return TypeOrmCoreModule;
    }
    const made = options.useFactory();
    if (name.length > 0 && (made.name ?? "").length === 0) {
      made.name = name;
    }
    provideDataSource(made);
    return TypeOrmCoreModule;
  }

  /** TypeOrmModule.forFeature([User, Order]): a Repository<Entity> for each, for @InjectRepository(Entity) */
  static forFeature(entities: ClassInfo[], dataSource: string = "default"): ClassInfo {
    for (const e of entities) {
      __jsonValues.set(getRepositoryToken(e, dataSource), '{"dataSourceName":' + JSON.stringify(dataSource) + ',"sessionId":-1}');
    }
    return TypeOrmFeatureModule;
  }
}

/** @InjectRepository(User) private readonly users: Repository<User> */
export function InjectRepository(entity: ClassInfo, dataSource: string = "default"): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return (c: ClassInfo, m: MethodInfo | null, index: int): void => {
    if (m === null) {
      __tokens.set(c.id + "#" + String(index), getRepositoryToken(entity, dataSource));
    }
  };
}

/** @InjectDataSource() private readonly dataSource: DataSource (a constructor parameter typed DataSource needs none) */
export function InjectDataSource(dataSource: string = "default"): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return (c: ClassInfo, m: MethodInfo | null, index: int): void => {
    if (m === null) {
      __tokens.set(c.id + "#" + String(index), getDataSourceToken(dataSource));
    }
  };
}
