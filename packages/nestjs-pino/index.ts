// nestjs-pino for xisto: LoggerModule.forRoot() provides a Logger that writes pino's JSON lines, and once it is
// built every Nest Logger line goes through it too (what app.useLogger(app.get(Logger)) does in Nest).
import { __overrideLogger, DynamicModule, Injectable, LoggerService, Module } from "@nestjs/common";
import { hostname } from "xisto:os";

/** pino's numeric levels. */
const LEVELS: Map<string, int> = new Map<string, int>([
  ["fatal", 60],
  ["error", 50],
  ["warn", 40],
  ["log", 30],
  ["info", 30],
  ["debug", 20],
  ["verbose", 10],
  ["trace", 10],
]);

/** pino's level labels, as ECS lines name them (Nest's log is info, verbose is trace). */
const LABELS: Map<string, string> = new Map<string, string>([
  ["fatal", "fatal"],
  ["error", "error"],
  ["warn", "warn"],
  ["log", "info"],
  ["info", "info"],
  ["debug", "debug"],
  ["verbose", "trace"],
  ["trace", "trace"],
]);

/**
 * Options of LoggerModule.forRoot: pinoHttp.level is honored, and @elastic/ecs-pino-format's ecsFormat() fills
 * the ECS ones (messageKey, ecsVersion, serviceName); the rest is accepted.
 */
export class PinoHttpOptions {
  level?: string;
  messageKey?: string;
  ecsVersion?: string;
  serviceName?: string;
}
/** renameContext: the key the context goes under ("log.logger" with ECS). */
export class LoggerModuleParams {
  pinoHttp?: PinoHttpOptions;
  renameContext?: string;
}

const state: PinoState = { min: 30, ecsVersion: "", serviceName: "", contextKey: "context" };
interface PinoState {
  min: int;
  ecsVersion: string; // "" : pino's own lines
  serviceName: string;
  contextKey: string;
}

class PinoLine {
  level: int;
  time: number;
  pid: int;
  hostname: string;
  context?: string;
  msg: string;
}

// @elastic/ecs-pino-format's line: {"log.level","@timestamp","process.pid","host.hostname","ecs.version",
// "service.name","event.dataset", the context, "message"}
function ecsLine(level: string, message: string, context: string): string {
  let line = `{"log.level":${JSON.stringify(LABELS.get(level) ?? "info")},"@timestamp":${JSON.stringify(new Date().toISOString())}`;
  line += `,"process.pid":${process.pid},"host.hostname":${JSON.stringify(hostname())},"ecs.version":${JSON.stringify(state.ecsVersion)}`;
  if (state.serviceName.length > 0) {
    line += `,"service.name":${JSON.stringify(state.serviceName)},"event.dataset":${JSON.stringify(state.serviceName)}`;
  }
  if (context.length > 0) {
    line += `,${JSON.stringify(state.contextKey)}:${JSON.stringify(context)}`;
  }
  return line + `,"message":${JSON.stringify(message)}}`;
}

function write(level: string, message: string, context: string): void {
  const n = LEVELS.has(level) ? LEVELS.get(level) : 30;
  if (n < state.min) {
    return;
  }
  if (state.ecsVersion.length > 0) {
    console.log(ecsLine(level, message, context));
    return;
  }
  const line: PinoLine = { level: n, time: Date.now(), pid: process.pid, hostname: process.env.get("HOSTNAME") ?? "localhost", msg: message };
  if (context.length > 0) {
    line.context = context;
  }
  console.log(JSON.stringify(line));
}

/**
 * Nest's LoggerService over pino: one JSON line per message ({"level":30,"time":...,"pid":...,"hostname":...,"context":...,"msg":...}).
 * @example app.useLogger(app.get(Logger))
 */
@Injectable()
export class Logger {
  constructor() {
    const target: LoggerService = {
      log: (m: string, c: string): void => write("log", m, c),
      error: (m: string, c: string): void => write("error", m, c),
      warn: (m: string, c: string): void => write("warn", m, c),
      debug: (m: string, c: string): void => write("debug", m, c),
      verbose: (m: string, c: string): void => write("verbose", m, c),
    };
    __overrideLogger(target);
  }
  log(message: string, context: string = ""): void {
    write("log", message, context);
  }
  error(message: string, context: string = ""): void {
    write("error", message, context);
  }
  warn(message: string, context: string = ""): void {
    write("warn", message, context);
  }
  debug(message: string, context: string = ""): void {
    write("debug", message, context);
  }
  verbose(message: string, context: string = ""): void {
    write("verbose", message, context);
  }
}

@Module({ providers: [Logger], exports: [Logger] })
export class LoggerModule {
  /** Provides the pino Logger; pinoHttp.level sets the lowest level written (default "info"). */
  static forRoot(params: LoggerModuleParams = {}): DynamicModule {
    const http = params.pinoHttp;
    if (http !== null) {
      const lv = http.level;
      if (lv !== null) {
        state.min = LEVELS.has(lv) ? LEVELS.get(lv) : 30;
      }
      state.ecsVersion = http.ecsVersion ?? "";
      state.serviceName = http.serviceName ?? "";
    }
    state.contextKey = params.renameContext ?? "context";
    return { module: LoggerModule, providers: [Logger], exports: [Logger], global: true };
  }
}
