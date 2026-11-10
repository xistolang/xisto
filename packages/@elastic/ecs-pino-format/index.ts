// @elastic/ecs-pino-format: pino options that write Elastic Common Schema lines ({"log.level","@timestamp",
// "process.pid","host.hostname","ecs.version","message"}), for nestjs-pino's LoggerModule.forRoot({ pinoHttp }).
// Node: ecsFormat() returns pino's messageKey/timestamp/formatters, spread into the options
// ({ ...base, ...ecsFormat() }); here it returns nestjs-pino's options with the ECS ones set (xisto has no object
// spread): LoggerModule.forRoot({ pinoHttp: ecsFormat(), renameContext: "log.logger" }).
import { PinoHttpOptions } from "nestjs-pino";

/** Options of ecsFormat (serviceName goes on each line; the APM and request conversions are accepted). */
export interface EcsFormatOptions {
  serviceName?: string;
  serviceVersion?: string;
  serviceEnvironment?: string;
  convertErr?: boolean;
  convertReqRes?: boolean;
  apmIntegration?: boolean;
}

/** The ECS version the lines declare. */
export const ECS_VERSION = "8.10.0";

/** pino options for ECS lines (level "info" unless set afterwards). */
export function ecsFormat(opts: EcsFormatOptions = {}): PinoHttpOptions {
  const o = new PinoHttpOptions();
  o.messageKey = "message";
  o.ecsVersion = ECS_VERSION;
  o.serviceName = opts.serviceName;
  return o;
}
