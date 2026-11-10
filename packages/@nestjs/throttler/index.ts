// @nestjs/throttler for xisto: ThrottlerModule.forRoot([{ name, ttl, limit }]) and ThrottlerGuard (usually
// { provide: APP_GUARD, useClass: ThrottlerGuard }) count each client's requests per route in memory, as the
// package's ThrottlerStorageService does: X-RateLimit-Limit/-Remaining/-Reset headers on each answer, and past
// the limit 429 "ThrottlerException: Too Many Requests" with Retry-After until the block ends. @SkipThrottle()
// and @Throttle({ name: { limit, ttl } }) change it per route or controller.
import { ClassInfo, MethodInfo } from "xisto:reflect";
import { DynamicModule, ExecutionContext, HttpException, Injectable, Logger, Module, Reflector, SetMetadata } from "@nestjs/common";
import { Request, Response } from "express";

/** One throttler: at most `limit` requests per `ttl` milliseconds (blocked for blockDuration, ttl by default). */
export interface ThrottlerOptions {
  name?: string;
  ttl: int;
  limit: int;
  blockDuration?: int;
}
/** What @Throttle sets for one throttler. */
export interface ThrottlerMethodOrControllerOptions {
  limit?: int;
  ttl?: int;
  blockDuration?: int;
}

export const THROTTLER_LIMIT = "THROTTLER:LIMIT";
export const THROTTLER_TTL = "THROTTLER:TTL";
export const THROTTLER_BLOCK_DURATION = "THROTTLER:BLOCK_DURATION";
export const THROTTLER_SKIP = "THROTTLER:SKIP";
export const throttlerMessage = "ThrottlerException: Too Many Requests";

class Named {
  name: string;
  ttl: int;
  limit: int;
  blockDuration: int;
  constructor(name: string, ttl: int, limit: int, blockDuration: int) {
    this.name = name;
    this.ttl = ttl;
    this.limit = limit;
    this.blockDuration = blockDuration;
  }
}
class Config {
  throttlers: Named[] = [];
}
const config = new Config();

/** ThrottlerModule.forRoot([{ ttl: 60000, limit: 10 }]): the throttlers ThrottlerGuard applies. */
@Module({})
export class ThrottlerModule {
  static forRoot(options: ThrottlerOptions[]): DynamicModule {
    const list: Named[] = [];
    for (const o of options) {
      list.push(new Named(o.name ?? "default", o.ttl, o.limit, o.blockDuration ?? -1));
    }
    // (as the guard's onModuleInit: shortest ttl first)
    list.sort((a: Named, b: Named): number => a.ttl - b.ttl);
    config.throttlers = list;
    return { module: ThrottlerModule, providers: [], exports: [], global: true };
  }
}

/** 429 Too Many Requests, with the throttler's message. */
@Injectable()
export class ThrottlerException extends HttpException {
  constructor(message: string = "") {
    super(message.length > 0 ? message : throttlerMessage, 429);
    this.exceptionName = "ThrottlerException";
    this.name = "ThrottlerException";
  }
}

/** Skips the named throttlers (all of { default: true } by default) on a route or a controller. */
export function SkipThrottle(skip: Record<string, boolean> = { default: true }): (c: ClassInfo, m: MethodInfo | null) => void {
  return (c: ClassInfo, m: MethodInfo | null): void => {
    for (const name of Object.keys(skip)) {
      const mark = SetMetadata(THROTTLER_SKIP + name, skip[name]);
      mark(c, m);
    }
  };
}

/** Other limits for a route or a controller: @Throttle({ default: { limit: 3, ttl: 60000 } }). */
export function Throttle(options: Record<string, ThrottlerMethodOrControllerOptions>): (c: ClassInfo, m: MethodInfo | null) => void {
  return (c: ClassInfo, m: MethodInfo | null): void => {
    for (const name of Object.keys(options)) {
      const o = options[name];
      if (o === null) {
        continue;
      }
      const limit = o.limit;
      const ttl = o.ttl;
      const block = o.blockDuration;
      if (limit !== null) {
        const mark = SetMetadata(THROTTLER_LIMIT + name, limit);
        mark(c, m);
      }
      if (ttl !== null) {
        const mark = SetMetadata(THROTTLER_TTL + name, ttl);
        mark(c, m);
      }
      if (block !== null) {
        const mark = SetMetadata(THROTTLER_BLOCK_DURATION + name, block);
        mark(c, m);
      }
    }
  };
}

/** What one counted request gives. */
export class ThrottlerStorageRecord {
  totalHits: int = 0;
  timeToExpire: int = 0;
  isBlocked: boolean = false;
  timeToBlockExpire: int = 0;
}

class Entry {
  totalHits: Map<string, int> = new Map<string, int>();
  hits: Map<string, number[]> = new Map<string, number[]>();
  expiresAt: number = 0;
  blockExpiresAt: number = 0;
  isBlocked: boolean = false;
}

/** The in-memory counts, by key (ThrottlerStorageService). */
@Injectable()
export class ThrottlerStorageService {
  private readonly storage: Map<string, Entry> = new Map<string, Entry>();

  private entry(key: string): Entry {
    const e = this.storage.get(key);
    if (e === null) {
      throw new Error("throttler: no record for " + key);
    }
    return e;
  }
  private hitList(e: Entry, name: string): number[] {
    const h = e.hits.get(name);
    if (h === null) {
      const fresh: number[] = [];
      e.hits.set(name, fresh);
      return fresh;
    }
    return h;
  }
  private fire(e: Entry, name: string, ttl: int): void {
    e.totalHits.set(name, (e.totalHits.get(name) ?? 0) + 1);
    this.hitList(e, name).push(Date.now() + Number(ttl));
  }

  increment(key: string, ttl: int, limit: int, blockDuration: int, name: string): ThrottlerStorageRecord {
    if (!this.storage.has(key)) {
      const fresh = new Entry();
      fresh.totalHits.set(name, 0);
      fresh.expiresAt = Date.now() + Number(ttl);
      this.storage.set(key, fresh);
    }
    const e = this.entry(key);
    const now = Date.now();
    const live = this.hitList(e, name).filter((at: number): boolean => at > now);
    e.hits.set(name, live);
    e.totalHits.set(name, live.length);
    let timeToExpire: int = Math.ceil((e.expiresAt - Date.now()) / 1000);
    if (timeToExpire <= 0) {
      e.expiresAt = Date.now() + Number(ttl);
      timeToExpire = Math.ceil((e.expiresAt - Date.now()) / 1000);
    }
    const r = new ThrottlerStorageRecord();
    r.timeToExpire = timeToExpire;
    if (blockDuration <= 0) {
      const blocked = (e.totalHits.get(name) ?? 0) >= limit;
      if (!blocked) {
        this.fire(e, name, ttl);
      }
      const hits = this.hitList(e, name);
      let first: number = 0;
      for (let i = 0; i < hits.length; i++) {
        if (i === 0 || hits[i] < first) first = hits[i];
      }
      r.totalHits = (e.totalHits.get(name) ?? 0) + (blocked ? 1 : 0);
      r.isBlocked = blocked;
      r.timeToBlockExpire = blocked ? (hits.length > 0 ? Math.ceil((first - Date.now()) / 1000) : timeToExpire) : 0;
      return r;
    }
    if (!e.isBlocked) {
      this.fire(e, name, ttl);
    }
    if ((e.totalHits.get(name) ?? 0) > limit && !e.isBlocked) {
      e.isBlocked = true;
      e.blockExpiresAt = Date.now() + Number(blockDuration);
    }
    let timeToBlockExpire: int = Math.ceil((e.blockExpiresAt - Date.now()) / 1000);
    if (timeToBlockExpire <= 0 && e.isBlocked) {
      e.isBlocked = false;
      e.totalHits.set(name, 0);
      e.hits.set(name, []);
      this.fire(e, name, ttl);
    }
    r.totalHits = e.totalHits.get(name) ?? 0;
    r.isBlocked = e.isBlocked;
    r.timeToBlockExpire = timeToBlockExpire;
    return r;
  }
}

const storage = new ThrottlerStorageService();

/**
 * Counts the request against each throttler: past a limit, 429 with Retry-After; otherwise the X-RateLimit headers
 * (with a "-name" suffix for throttlers not named "default").
 * @example { provide: APP_GUARD, useClass: ThrottlerGuard }
 */
@Injectable()
export class ThrottlerGuard {
  protected readonly reflector: Reflector;
  protected readonly headerPrefix: string = "X-RateLimit";
  constructor(reflector: Reflector) {
    this.reflector = reflector;
  }

  canActivate(context: ExecutionContext): boolean {
    if (this.shouldSkip(context)) {
      return true;
    }
    const handler = context.getHandler();
    const classRef = context.getClass();
    for (const t of config.throttlers) {
      const skip = this.reflector.getAllAndOverride<boolean>(THROTTLER_SKIP + t.name, [handler, classRef]);
      if (skip === true) {
        continue;
      }
      const limit = this.reflector.getAllAndOverride<int>(THROTTLER_LIMIT + t.name, [handler, classRef]) ?? t.limit;
      const ttl = this.reflector.getAllAndOverride<int>(THROTTLER_TTL + t.name, [handler, classRef]) ?? t.ttl;
      let block = this.reflector.getAllAndOverride<int>(THROTTLER_BLOCK_DURATION + t.name, [handler, classRef]) ?? t.blockDuration;
      if (block < 0) {
        block = ttl;
      }
      this.handleRequest(context, limit, ttl, t.name, block);
    }
    return true;
  }

  /** true: no throttler applies to this request. */
  protected shouldSkip(context: ExecutionContext): boolean {
    return false;
  }

  /** Who is counted: the client's address. */
  protected getTracker(req: Request): string {
    return req.ip;
  }

  protected handleRequest(context: ExecutionContext, limit: int, ttl: int, name: string, blockDuration: int): boolean {
    const req = context.switchToHttp().getRequest<Request>();
    const res = context.switchToHttp().getResponse<Response>();
    const tracker = this.getTracker(req);
    const key = `${context.getClass().name}-${context.getHandler().name}-${name}-${tracker}`;
    const r = storage.increment(key, ttl, limit, blockDuration, name);
    const suffix = name === "default" ? "" : "-" + name;
    if (r.isBlocked) {
      res.setHeader("Retry-After" + suffix, String(r.timeToBlockExpire));
      throw new ThrottlerException(throttlerMessage);
    }
    res.setHeader(`${this.headerPrefix}-Limit${suffix}`, String(limit));
    res.setHeader(`${this.headerPrefix}-Remaining${suffix}`, String(Math.max(0, limit - r.totalHits)));
    res.setHeader(`${this.headerPrefix}-Reset${suffix}`, String(r.timeToExpire));
    return true;
  }
}
