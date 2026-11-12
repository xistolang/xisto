// TypeOrmModule.forRootAsync with inject: [ConfigService]: the factory runs when the application starts and sees the config (no database needed: the DataSource connects on its first query)
// A namespace ConfigModule.forRoot loads is also a provider under its KEY (@Inject(databaseConfig.KEY)), as in Nest.
import { Inject, Injectable, Module } from "@nestjs/common";
import { NestFactory } from "@nestjs/core";
import { ConfigModule, ConfigService, ConfigType, registerAs } from "@nestjs/config";
import { TypeOrmModule, TypeOrmModuleOptions } from "@nestjs/typeorm";

const databaseConfig = registerAs("database", (): TypeOrmModuleOptions => ({
  type: "oracle",
  host: process.env.DB_HOST ?? "db.local",
  port: 1521,
  username: "app",
  password: "x",
  serviceName: "FREEPDB1",
}));

@Injectable()
class DbInfo {
  constructor(@Inject(databaseConfig.KEY) readonly db: ConfigType<typeof databaseConfig>) {}
}

@Module({
  providers: [DbInfo],
  imports: [
    ConfigModule.forRoot({ isGlobal: true, load: [databaseConfig] }),
    TypeOrmModule.forRootAsync({
      imports: [ConfigModule],
      inject: [ConfigService],
      useFactory: (config: ConfigService): TypeOrmModuleOptions => {
        const o = config.getOrThrow<TypeOrmModuleOptions>("database");
        console.log("factory got", o.type, o.host, o.port);
        return { ...o };
      },
    }),
  ],
})
class AppModule {}

const app = NestFactory.create(AppModule);
app.init();
console.log("init done");
console.log("by KEY", (app.get(DbInfo) as DbInfo).db.host);
