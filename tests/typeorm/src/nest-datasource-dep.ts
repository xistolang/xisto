// a provider under a Symbol (useClass) whose constructor takes the DataSource that forRootAsync with inject made at init
import { Inject, Injectable, Module } from "@nestjs/common";
import { NestFactory } from "@nestjs/core";
import { TypeOrmModule, TypeOrmModuleOptions } from "@nestjs/typeorm";
import { ConfigModule, ConfigService, registerAs } from "@nestjs/config";

const databaseConfig = registerAs("database", (): TypeOrmModuleOptions => ({ type: "oracle", host: "db.local", port: 1521, username: "u", password: "p", serviceName: "FREEPDB1" }));
import { DataSource } from "typeorm";

export const FN_VALOR = Symbol("FN_VALOR");
@Injectable()
export class FnValor {
  constructor(private readonly ds: DataSource) {}
  describe(): string {
    return `fn over ${this.ds.options.type}`;
  }
}

// the DataSource named as the token, as an app writes it: @Inject(DataSource)
@Injectable()
export class FnTipo {
  constructor(@Inject(DataSource) private readonly dataSource: DataSource) {}
  tipo(): string {
    return `tipo ${this.dataSource.options.type}`;
  }
}

@Injectable()
class Calc {
  constructor(@Inject(FN_VALOR) private readonly fn: FnValor) {}
  run(): string {
    return this.fn.describe();
  }
}

@Module({
  imports: [
    ConfigModule.forRoot({ isGlobal: true, load: [databaseConfig] }),
    TypeOrmModule.forRootAsync({
      imports: [ConfigModule],
      inject: [ConfigService],
      useFactory: (config: ConfigService): TypeOrmModuleOptions => ({ ...config.getOrThrow<TypeOrmModuleOptions>("database") }),
    }),
  ],
  providers: [{ provide: FN_VALOR, useClass: FnValor }, Calc, FnTipo],
})
class AppModule {}

const app = NestFactory.create(AppModule);
app.init();
console.log((app.get(Calc) as Calc).run());
console.log((app.get(FnTipo) as FnTipo).tipo());
