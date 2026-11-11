// TypeOrmModule.forRootAsync with inject: [ConfigService]: the factory runs when the application starts and sees the config (no database needed: the DataSource connects on its first query)
import { Module } from "@nestjs/common";
import { NestFactory } from "@nestjs/core";
import { ConfigModule, ConfigService, registerAs } from "@nestjs/config";
import { TypeOrmModule, TypeOrmModuleOptions } from "@nestjs/typeorm";

const databaseConfig = registerAs("database", (): TypeOrmModuleOptions => ({
  type: "oracle",
  host: process.env.DB_HOST ?? "db.local",
  port: 1521,
  username: "app",
  password: "x",
  serviceName: "FREEPDB1",
}));

@Module({
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
