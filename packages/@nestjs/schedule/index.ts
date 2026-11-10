// @nestjs/schedule for xisto: @Cron("0 * * * *") runs a provider's method on a cron schedule once the
// application has started. ScheduleModule.forRoot() enables it. Expressions have 5 fields (minute hour
// day-of-month month day-of-week) or 6 (seconds first), with *, numbers, ranges a-b, steps */n and lists.
import { ClassInfo, MethodInfo } from "xisto:reflect";
import { DynamicModule, Injectable, Logger, Module } from "@nestjs/common";
import { getInstance } from "@nestjs/core";

/** Common expressions, as @nestjs/schedule names them. */
export enum CronExpression {
  EVERY_SECOND = "* * * * * *",
  EVERY_5_SECONDS = "*/5 * * * * *",
  EVERY_10_SECONDS = "*/10 * * * * *",
  EVERY_30_SECONDS = "*/30 * * * * *",
  EVERY_MINUTE = "*/1 * * * *",
  EVERY_5_MINUTES = "0 */5 * * * *",
  EVERY_10_MINUTES = "0 */10 * * * *",
  EVERY_30_MINUTES = "0 */30 * * * *",
  EVERY_HOUR = "0 0-23/1 * * *",
  EVERY_DAY_AT_MIDNIGHT = "0 0 * * *",
  EVERY_DAY_AT_1AM = "0 01 * * *",
  EVERY_DAY_AT_6AM = "0 06 * * *",
  EVERY_DAY_AT_NOON = "0 12 * * *",
  EVERY_WEEK = "0 0 * * 0",
  EVERY_1ST_DAY_OF_MONTH_AT_MIDNIGHT = "0 0 1 * *",
}

/** Options of @Cron: a name (accepted) and a time zone (accepted; the machine's zone is used). */
export class CronOptions {
  name?: string;
  timeZone?: string;
  disabled?: boolean;
}

interface CronJob {
  cls: ClassInfo;
  method: MethodInfo;
  expression: string;
  disabled: boolean;
}

const jobs: CronJob[] = [];

/** Runs the method on the cron schedule `expression`. */
export function Cron(expression: string, options: CronOptions = {}): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {
    jobs.push({ cls: c, method: m, expression: expression, disabled: options.disabled || false });
  };
}

// the set of allowed values of one field, as a 0/1 table from 0 to max
function field(spec: string, min: int, max: int): boolean[] {
  const ok: boolean[] = [];
  for (let i: int = 0; i <= max; i++) {
    ok.push(false);
  }
  for (const part of spec.split(",")) {
    let step: int = 1;
    let range = part;
    const slash = part.indexOf("/");
    if (slash >= 0) {
      step = Math.trunc(parseInt(part.slice(slash + 1, part.length), 10));
      range = part.slice(0, slash);
    }
    let lo: int = min;
    let hi: int = max;
    if (range !== "*") {
      const dash = range.indexOf("-");
      if (dash >= 0) {
        lo = Math.trunc(parseInt(range.slice(0, dash), 10));
        hi = Math.trunc(parseInt(range.slice(dash + 1, range.length), 10));
      } else {
        lo = Math.trunc(parseInt(range, 10));
        hi = slash >= 0 ? max : lo;
      }
    }
    if (step < 1) {
      step = 1;
    }
    for (let v: int = lo; v <= hi && v <= max; v += step) {
      if (v >= min) {
        ok[v] = true;
      }
    }
  }
  return ok;
}

/** A parsed cron expression. */
export class CronSchedule {
  readonly seconds: boolean[];
  readonly minutes: boolean[];
  readonly hours: boolean[];
  readonly days: boolean[];
  readonly months: boolean[];
  readonly weekdays: boolean[];
  constructor(expression: string) {
    const parts = expression.trim().split(" ").filter((s: string): boolean => s.length > 0);
    const six = parts.length === 6;
    const off = six ? 1 : 0;
    if (parts.length !== 5 && parts.length !== 6) {
      throw new Error(`Invalid cron expression: ${expression}`);
    }
    this.seconds = field(six ? parts[0] : "0", 0, 59);
    this.minutes = field(parts[off], 0, 59);
    this.hours = field(parts[off + 1], 0, 23);
    this.days = field(parts[off + 2], 1, 31);
    this.months = field(parts[off + 3], 1, 12);
    const wd = field(parts[off + 4], 0, 7);
    if (wd[7]) {
      wd[0] = true; // 7 is Sunday too
    }
    this.weekdays = wd;
  }

  /** The next time after `from` (milliseconds since the epoch) the schedule fires. */
  next(from: number): number {
    let t: number = Math.floor(from / 1000) * 1000 + 1000;
    for (let guard: int = 0; guard < 400 * 24 * 60 * 60; guard++) {
      const d = new Date(t);
      if (!this.months[d.getMonth() + 1] || !this.days[d.getDate()] || !this.weekdays[d.getDay()]) {
        t = new Date(d.getFullYear(), d.getMonth(), d.getDate() + 1, 0, 0, 0).getTime();
        continue;
      }
      if (!this.hours[d.getHours()]) {
        t = new Date(d.getFullYear(), d.getMonth(), d.getDate(), d.getHours() + 1, 0, 0).getTime();
        continue;
      }
      if (!this.minutes[d.getMinutes()]) {
        t = new Date(d.getFullYear(), d.getMonth(), d.getDate(), d.getHours(), d.getMinutes() + 1, 0).getTime();
        continue;
      }
      if (!this.seconds[d.getSeconds()]) {
        t += 1000;
        continue;
      }
      return t;
    }
    return from + 24 * 60 * 60 * 1000;
  }
}

// the next run of a job, then the one after it (a timer on the event loop: the server keeps answering meanwhile)
function arm(job: CronJob, schedule: CronSchedule, instance: unknown): void {
  const now = Date.now();
  const at = schedule.next(now);
  setTimeout((): void => {
    try {
      job.method.invoke(instance, []);
    } catch (e) {
      new Logger("Scheduler").error(e.message, e.stack);
    }
    arm(job, schedule, instance);
  }, at - now);
}

function run(job: CronJob): void {
  arm(job, new CronSchedule(job.expression), getInstance(job.cls));
}

/** Starts the @Cron jobs when the application has started. */
@Injectable()
export class SchedulerRunner {
  onApplicationBootstrap(): void {
    for (const job of jobs) {
      if (!job.disabled) {
        run(job);
      }
    }
  }
}

@Module({ providers: [SchedulerRunner] })
export class ScheduleModule {
  /** Enables @Cron. */
  static forRoot(): DynamicModule {
    return { module: ScheduleModule, providers: [SchedulerRunner], exports: [], global: true };
  }
}
