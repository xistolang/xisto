// nestjs-pino for xisto: LoggerModule.forRoot() provides a Logger that writes pino's JSON lines, and once it is
// built every Nest Logger line goes through it too (what app.useLogger(app.get(Logger)) does in Nest).
import { DynamicModule, Injectable, Logger as NestLogger, LoggerService, Module } from "@nestjs/common";

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

/** Options of LoggerModule.forRoot (pinoHttp.level is honored; the rest is accepted). */
export class PinoHttpOptions {
  level?: string;
}
export class LoggerModuleParams {
  pinoHttp?: PinoHttpOptions;
}

const state: PinoState = { min: 30 };
interface PinoState {
  min: int;
}

class PinoLine {
  level: int;
  time: number;
  pid: int;
  hostname: string;
  context?: string;
  msg: string;
}

function write(level: string, message: string, context: string): void {
  const n = LEVELS.get(level) || 30;
  if (n < state.min) {
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
    NestLogger.overrideLogger(target);
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
        state.min = LEVELS.get(lv) || 30;
      }
    }
    return { module: LoggerModule, providers: [Logger], exports: [Logger], global: true };
  }
}
