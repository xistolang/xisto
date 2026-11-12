// nestjs-pino for xisto: LoggerModule.forRoot() provides a Logger that writes pino's JSON lines, and once it is
// built every Nest Logger line goes through it too (what app.useLogger(app.get(Logger)) does in Nest).
import { __overrideLogger, __overrideLoggerFields, DynamicModule, Injectable, LoggerFieldsSink, LoggerService, Module } from "@nestjs/common";
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
  autoLogging?: boolean; // (accepted: requests are not logged by this module)
  redact?: string[]; // (accepted: the lines it writes carry no request headers)
  transport?: PinoTransport; // (accepted: pino-pretty is not run; lines stay JSON)
}
/** pinoHttp.transport: { target: "pino-pretty", options } (accepted, not run) */
export class PinoTransport {
  target?: string;
  options?: Record<string, unknown>;
}
/** renameContext: the key the context goes under ("log.logger" with ECS). */
export class LoggerModuleParams {
  pinoHttp?: PinoHttpOptions;
  renameContext?: string;
}
/** nestjs-pino's name for what LoggerModule.forRoot takes. */
export type Params = LoggerModuleParams;

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
// a logger call's own fields ({ "http.status": 200 }), as pino merges them into the line: ',"k":v' each
function fieldsText(fields: Record<string, unknown> | null): string {
  let out = "";
  if (fields !== null) {
    for (const k of Object.keys(fields)) {
      out += `,${JSON.stringify(k)}:${JSON.stringify(fields[k])}`;
    }
  }
  return out;
}

// an object's JSON as the ',"k":v' pairs pino merges into the line
function jsonFields(json: string): string {
  const t = json.trim();
  if (t.length <= 2 || !t.startsWith("{")) {
    return "";
  }
  return "," + t.slice(1, t.length - 1);
}

function ecsLine(level: string, message: string | null, context: string, extra: string): string {
  let line = `{"log.level":${JSON.stringify(LABELS.get(level) ?? "info")},"@timestamp":${JSON.stringify(new Date().toISOString())}`;
  line += `,"process.pid":${process.pid},"host.hostname":${JSON.stringify(hostname())},"ecs.version":${JSON.stringify(state.ecsVersion)}`;
  if (state.serviceName.length > 0) {
    line += `,"service.name":${JSON.stringify(state.serviceName)},"event.dataset":${JSON.stringify(state.serviceName)}`;
  }
  if (context.length > 0) {
    line += `,${JSON.stringify(state.contextKey)}:${JSON.stringify(context)}`;
  }
  return line + extra + (message === null ? "}" : `,"message":${JSON.stringify(message)}}`);
}

function write(level: string, message: string, context: string, fields: Record<string, unknown> | null = null): void {
  writeLine(level, message, context, fieldsText(fields), fields !== null);
}

// one line: extra is the call's own fields as ',"k":v' pairs (pino puts them after the context, before msg);
// message null: a line with no msg (logger.warn({ ... }) has none)
function writeLine(level: string, message: string | null, context: string, extra: string, withFields: boolean): void {
  const n = LEVELS.has(level) ? LEVELS.get(level) : 30;
  if (n < state.min) {
    return;
  }
  if (state.ecsVersion.length > 0) {
    console.log(ecsLine(level, message, context, extra));
    return;
  }
  if (withFields || message === null) { // pino's line with the call's fields before msg
    let text = `{"level":${n},"time":${Date.now()},"pid":${process.pid},"hostname":${JSON.stringify(process.env.get("HOSTNAME") ?? "localhost")}`;
    if (context.length > 0) {
      text += `,"context":${JSON.stringify(context)}`;
    }
    console.log(text + extra + (message === null ? "}" : `,"msg":${JSON.stringify(message)}}`));
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
    const sink: LoggerFieldsSink = {
      write: (level: string, fields: string, message: string | null, context: string): void => writeLine(level, message, context, jsonFields(fields), true),
    };
    __overrideLoggerFields(sink);
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

/**
 * nestjs-pino's PinoLogger: pino's methods with a context of its own; the first argument may be the line's
 * own fields: logger.info({ "http.status": 200 }, "GET / 200").
 */
@Injectable()
export class PinoLogger {
  private context: string = "";
  setContext(context: string): void {
    this.context = context;
  }
  private emit(level: string, a: Record<string, unknown> | string, msg: string): void {
    if (typeof a === "string") {
      write(level, a, this.context);
    } else {
      write(level, msg, this.context, a);
    }
  }
  trace(a: Record<string, unknown> | string, msg: string = ""): void {
    this.emit("trace", a, msg);
  }
  debug(a: Record<string, unknown> | string, msg: string = ""): void {
    this.emit("debug", a, msg);
  }
  info(a: Record<string, unknown> | string, msg: string = ""): void {
    this.emit("info", a, msg);
  }
  warn(a: Record<string, unknown> | string, msg: string = ""): void {
    this.emit("warn", a, msg);
  }
  error(a: Record<string, unknown> | string, msg: string = ""): void {
    this.emit("error", a, msg);
  }
  fatal(a: Record<string, unknown> | string, msg: string = ""): void {
    this.emit("fatal", a, msg);
  }
}

@Module({ providers: [Logger, PinoLogger], exports: [Logger, PinoLogger] })
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
    return { module: LoggerModule, providers: [Logger, PinoLogger], exports: [Logger, PinoLogger], global: true };
  }
  /** LoggerModule.forRootAsync({ useFactory: () => params }): the same, with the params computed at import */
  static forRootAsync(options: LoggerModuleAsyncParams): DynamicModule {
    return LoggerModule.forRoot(options.useFactory());
  }
}
/** forRootAsync's options (inject and imports are accepted; the factory takes no arguments in xisto) */
export interface LoggerModuleAsyncParams {
  imports?: unknown[];
  inject?: unknown[];
  useFactory: () => LoggerModuleParams;
}
