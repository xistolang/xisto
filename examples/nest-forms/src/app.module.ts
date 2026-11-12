import { Module } from "@nestjs/common";
import { ConfigModule } from "@nestjs/config";
import { ecsFormat } from "@elastic/ecs-pino-format";
import { LoggerModule, Params } from "nestjs-pino";
import formsConfig from "./forms.config";
import { FormsController, LocalController } from "./forms.controller";
import { FormsService } from "./forms.service";

// pino's own lines, or ECS lines (LOG_FORMAT=ecs) with the context as log.logger
function loggerParams(): Params {
  if (process.env.LOG_FORMAT === "ecs") {
    return { pinoHttp: { level: "info", autoLogging: false, ...ecsFormat() }, renameContext: "log.logger" };
  }
  return { pinoHttp: { level: "info", autoLogging: false } };
}

@Module({
  imports: [ConfigModule.forFeature(formsConfig), LoggerModule.forRoot(loggerParams())],
  controllers: [FormsController, LocalController],
  providers: [FormsService],
})
export class AppModule {}
