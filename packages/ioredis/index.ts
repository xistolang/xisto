// ioredis (v5): new Redis(options | url), connect(), the string, key, hash, set and sorted-set commands a service
// uses, pipeline().exec(), call(), quit(). Over the redis package's connection (RESP2, pipelined, one connection
// per worker thread; connectTimeout and commandTimeout are applied there).
//
// Connection states and errors follow ioredis: status "wait" (lazyConnect) / "connecting" -> "ready"; a failed
// connect() rejects with "Connection is closed." after an "error" event; while the server is away the status is
// "reconnecting" and every reconnection attempt waits min(attempts * 50, 2000) ms (ioredis's retryStrategy);
// each failed attempt emits "error", "close" and "reconnecting", and on every (maxRetriesPerRequest + 1)th one the
// commands waiting fail with MaxRetriesPerRequestError. Server errors are ReplyError; a reply that does not start
// within commandTimeout is "Command timed out".
// Node: ioredis also reconnects in the background with no command waiting (and emits its events then); here the
// attempts are made by the commands that need the connection.
import { NativeRedis } from "redis";
import { setTimeout as sleep } from "xisto:timers/promises";

/** new Redis(options): where the server is and how the client behaves. */
export interface RedisOptions {
  host?: string;
  port?: number;
  username?: string;
  password?: string;
  db?: number;
  /** don't connect in the constructor: connect() or the first command does */
  lazyConnect?: boolean;
  enableReadyCheck?: boolean;
  /** failed reconnection attempts a waiting command survives (default 20; null: no limit) */
  maxRetriesPerRequest?: number;
  /** ms for the TCP connection (default 10000) */
  connectTimeout?: number;
  /** ms for a command's reply to start (default: no limit) */
  commandTimeout?: number;
}

// what the connection is given (read as JSON by the redis package)
interface ConnectionConfig {
  url: string | null;
  host: string | null;
  port: number | null;
  username: string | null;
  password: string | null;
  db: number | null;
  connectTimeout: number;
  commandTimeout: number | null;
  warnUnneededPassword: boolean;
}

/** The command a ReplyError answered. */
export interface ReplyCommand {
  name: string;
  args: string[];
}

/** An error the server answered (WRONGTYPE..., ERR ...), with the command that got it. */
export class ReplyError extends Error {
  command: ReplyCommand;
  constructor(message: string, command: ReplyCommand) {
    super(message);
    this.name = "ReplyError";
    this.command = command;
  }
}

/** A command that waited through maxRetriesPerRequest failed reconnection attempts. */
export class MaxRetriesPerRequestError extends Error {
  constructor(max: number) {
    super(`Reached the max retries per request limit (which is ${max}). Refer to "maxRetriesPerRequest" option for details.`);
    this.name = "MaxRetriesPerRequestError";
  }
}

const CONNECTION_LOST = "redis: the connection was lost";

function join(head: string[], rest: string[]): string[] {
  const out: string[] = head;
  for (const r of rest) {
    out.push(r);
  }
  return out;
}

function strings(values: (string | number)[]): string[] {
  const out: string[] = [];
  for (const v of values) {
    out.push(String(v));
  }
  return out;
}

/** A Redis client, as ioredis's default export. */
export class Redis {
  private readonly native: NativeRedis;
  private readonly listeners: Map<string, ((err: Error) => void)[]> = new Map<string, ((err: Error) => void)[]>();
  private readonly maxRetries: number;
  private readonly unlimitedRetries: boolean;
  private retryAttempts: number = 0;
  private nextDelay: number = 0;
  /** wait, connecting, connect, ready, reconnecting, end */
  status: string;

  constructor(target: string | RedisOptions = {}, options: RedisOptions = {}) {
    const o: RedisOptions = typeof target === "string" ? options : target;
    const cfg: ConnectionConfig = {
      url: typeof target === "string" ? target : null,
      host: o.host ?? null,
      port: o.port ?? null,
      username: o.username ?? null,
      password: o.password ?? null,
      db: o.db ?? null,
      connectTimeout: o.connectTimeout ?? 10000,
      commandTimeout: o.commandTimeout ?? null,
      warnUnneededPassword: true,
    };
    this.native = new NativeRedis(cfg);
    this.unlimitedRetries = o.maxRetriesPerRequest === null && "maxRetriesPerRequest" in o;
    this.maxRetries = o.maxRetriesPerRequest ?? 20;
    this.status = o.lazyConnect === true ? "wait" : "connecting";
  }

  /**
   * Listens to "connect", "ready", "error" (the Error), "close", "reconnecting" or "end".
   * Node: "reconnecting" listeners get the delay in ms; here only "error" listeners get an argument (an Error).
   */
  on(event: string, listener: (err: Error) => void): Redis {
    const list = this.listeners.get(event);
    if (list === null) {
      this.listeners.set(event, [listener]);
    } else {
      list.push(listener);
    }
    return this;
  }

  private emit(event: string, err: Error | null = null): void {
    const list = this.listeners.get(event);
    if (list !== null) {
      const arg = err ?? new Error(event);
      for (const l of list) {
        l(arg);
      }
    }
  }

  // one connection attempt: true when the connection is ready
  private attempt(): boolean {
    try {
      this.native.connect();
    } catch (e) {
      this.retryAttempts += 1;
      this.nextDelay = Math.min(this.retryAttempts * 50, 2000);
      this.status = "reconnecting";
      this.emit("error", e);
      this.emit("close");
      this.emit("reconnecting");
      return false;
    }
    this.retryAttempts = 0;
    this.status = "connect";
    this.emit("connect");
    this.status = "ready";
    this.emit("ready");
    return true;
  }

  /** Connects now; rejects with "Connection is closed." when the server cannot be reached. */
  async connect(): Promise<void> {
    if (this.status === "ready" || this.status === "connect") {
      throw new Error("Redis is already connecting/connected");
    }
    if (!this.attempt()) {
      throw new Error("Connection is closed.");
    }
  }

  // the connection a command needs: (re)connects as ioredis's retry strategy does
  private async ensure(): Promise<void> {
    for (;;) {
      if (this.status === "end") {
        throw new Error("Connection is closed.");
      }
      if (this.status === "ready" && this.native.isOpen()) {
        return;
      }
      if (this.status === "reconnecting") {
        await sleep(Math.trunc(this.nextDelay));
        if (this.status === "ready" && this.native.isOpen()) {
          return;
        }
      }
      if (this.attempt()) {
        return;
      }
      if (!this.unlimitedRetries && this.retryAttempts % (this.maxRetries + 1) === 0) {
        throw new MaxRetriesPerRequestError(this.maxRetries);
      }
    }
  }

  // a command that failed: a lost connection is retried (false), anything else is thrown as ioredis throws it
  private failed(e: Error, args: string[]): void {
    if (e.message === "Command timed out") {
      throw e;
    }
    if (!this.native.isOpen() && (e.message === CONNECTION_LOST || e.message.startsWith("connect "))) {
      this.retryAttempts += 1;
      this.nextDelay = Math.min(this.retryAttempts * 50, 2000);
      this.status = "reconnecting";
      this.emit("close");
      this.emit("reconnecting");
      return;
    }
    throw new ReplyError(e.message, { name: args[0].toLowerCase(), args: args.slice(1) });
  }

  private async str(args: string[]): Promise<string | null> {
    for (;;) {
      await this.ensure();
      try {
        return this.native.str(args);
      } catch (e) {
        this.failed(e, args);
      }
    }
  }

  private async int(args: string[]): Promise<number> {
    for (;;) {
      await this.ensure();
      try {
        return Number(this.native.int(args));
      } catch (e) {
        this.failed(e, args);
      }
    }
  }

  private async list(args: string[]): Promise<string[]> {
    for (;;) {
      await this.ensure();
      try {
        return this.native.strs(args);
      } catch (e) {
        this.failed(e, args);
      }
    }
  }

  private async nullableList(args: string[]): Promise<(string | null)[]> {
    for (;;) {
      await this.ensure();
      try {
        return this.native.strsn(args);
      } catch (e) {
        this.failed(e, args);
      }
    }
  }

  private async any(args: string[]): Promise<unknown> {
    for (;;) {
      await this.ensure();
      try {
        return JSON.parse(this.native.json(args));
      } catch (e) {
        this.failed(e, args);
      }
    }
  }

  /** "PONG" */
  async ping(): Promise<string> {
    return (await this.str(["PING"])) ?? "";
  }

  async get(key: string): Promise<string | null> {
    return this.str(["GET", key]);
  }

  /**
   * SET, with ioredis's positional options: "OK", or null when NX/XX kept it from being set.
   * @example await redis.set("session:1", json, "EX", 3600); await redis.set("lock", id, "PX", 5000, "NX");
   */
  async set(key: string, value: string | number, mode: string = "", time: number = -1, flag: string = ""): Promise<string | null> {
    const args: string[] = ["SET", key, String(value)];
    if (mode.length > 0) {
      args.push(mode);
      if (time >= 0) {
        args.push(String(time));
      }
    }
    if (flag.length > 0) {
      args.push(flag);
    }
    return this.str(args);
  }

  async setex(key: string, seconds: number, value: string | number): Promise<string | null> {
    return this.str(["SETEX", key, String(seconds), String(value)]);
  }

  async mget(...keys: string[]): Promise<(string | null)[]> {
    return this.nullableList(join(["MGET"], keys));
  }

  /** the keys removed */
  async del(...keys: string[]): Promise<number> {
    return this.int(join(["DEL"], keys));
  }

  /** how many of the keys exist */
  async exists(...keys: string[]): Promise<number> {
    return this.int(join(["EXISTS"], keys));
  }

  /** seconds left; -1 without expiry, -2 when the key does not exist */
  async ttl(key: string): Promise<number> {
    return this.int(["TTL", key]);
  }

  async pttl(key: string): Promise<number> {
    return this.int(["PTTL", key]);
  }

  /** 1 when the timeout was set, 0 when the key does not exist */
  async expire(key: string, seconds: number): Promise<number> {
    return this.int(["EXPIRE", key, String(seconds)]);
  }

  async incr(key: string): Promise<number> {
    return this.int(["INCR", key]);
  }

  async incrby(key: string, by: number): Promise<number> {
    return this.int(["INCRBY", key, String(by)]);
  }

  async decr(key: string): Promise<number> {
    return this.int(["DECR", key]);
  }

  async keys(pattern: string): Promise<string[]> {
    return this.list(["KEYS", pattern]);
  }

  async hget(key: string, field: string): Promise<string | null> {
    return this.str(["HGET", key, field]);
  }

  /** HSET key field value, or HSET key { field: value, ... }: the fields added */
  async hset(key: string, fields: string | Record<string, string | number>, value: string | number = ""): Promise<number> {
    return this.int(hsetArgs(key, fields, value));
  }

  async hmget(key: string, ...fields: string[]): Promise<(string | null)[]> {
    return this.nullableList(join(["HMGET", key], fields));
  }

  async hgetall(key: string): Promise<Record<string, string>> {
    const flat = await this.list(["HGETALL", key]);
    const out: Record<string, string> = {};
    for (let i = 0; i + 1 < flat.length; i += 2) {
      out[flat[i]] = flat[i + 1];
    }
    return out;
  }

  async hdel(key: string, ...fields: string[]): Promise<number> {
    return this.int(join(["HDEL", key], fields));
  }

  async sadd(key: string, ...members: (string | number)[]): Promise<number> {
    return this.int(join(["SADD", key], strings(members)));
  }

  async srem(key: string, ...members: (string | number)[]): Promise<number> {
    return this.int(join(["SREM", key], strings(members)));
  }

  async smembers(key: string): Promise<string[]> {
    return this.list(["SMEMBERS", key]);
  }

  /** SPOP key count: up to count random members, removed */
  async spop(key: string, count: number): Promise<string[]> {
    return this.list(["SPOP", key, String(count)]);
  }

  /** ZADD key score member [score member ...]: the members added */
  async zadd(key: string, ...scoreMembers: (string | number)[]): Promise<number> {
    return this.int(join(["ZADD", key], strings(scoreMembers)));
  }

  /** the members from start to stop (by rank); with "WITHSCORES", member and score alternate */
  async zrange(key: string, start: number, stop: number, withScores: string = ""): Promise<string[]> {
    const args: string[] = ["ZRANGE", key, String(start), String(stop)];
    if (withScores.length > 0) {
      args.push(withScores);
    }
    return this.list(args);
  }

  async zrangebyscore(key: string, min: number | string, max: number | string): Promise<string[]> {
    return this.list(["ZRANGEBYSCORE", key, String(min), String(max)]);
  }

  async zremrangebyscore(key: string, min: number | string, max: number | string): Promise<number> {
    return this.int(["ZREMRANGEBYSCORE", key, String(min), String(max)]);
  }

  async zrem(key: string, ...members: string[]): Promise<number> {
    return this.int(join(["ZREM", key], members));
  }

  async flushdb(): Promise<string> {
    return (await this.str(["FLUSHDB"])) ?? "";
  }

  /** Any command: its reply (integers as numbers, nil as null, arrays as arrays). */
  async call(command: string, ...args: (string | number)[]): Promise<unknown> {
    return this.any(join([command], strings(args)));
  }

  /** Commands sent together; exec() gives [error, reply] for each, in order. */
  pipeline(): Pipeline {
    return new Pipeline(this);
  }

  /** @internal a pipeline's command: [null, reply], or [ReplyError, null] */
  async __pipelined(args: string[]): Promise<[Error | null, unknown]> {
    try {
      const reply = await this.any(args); // (outside the literal: a literal whose element throws leaks, a compiler bug)
      return [null, reply];
    } catch (e) {
      if (e instanceof ReplyError) {
        return [e, null];
      }
      throw e;
    }
  }

  /** Closes the connection after the pending replies: "OK". */
  async quit(): Promise<string> {
    const wasOpen = this.native.isOpen();
    if (wasOpen) {
      try {
        this.native.str(["QUIT"]);
      } catch (e) {
        // (the server closes the connection after its reply)
      }
    }
    this.native.close();
    if (wasOpen || this.status !== "reconnecting") {
      this.status = "end";
      if (wasOpen) {
        this.emit("close");
      }
      this.emit("end");
    }
    return "OK";
  }

  /** Closes the connection now. */
  disconnect(): void {
    const wasOpen = this.native.isOpen();
    this.native.close();
    this.status = "end";
    if (wasOpen) {
      this.emit("close");
    }
    this.emit("end");
  }
}

function hsetArgs(key: string, fields: string | Record<string, string | number>, value: string | number): string[] {
  const args: string[] = ["HSET", key];
  if (typeof fields === "string") {
    args.push(fields);
    args.push(String(value));
    return args;
  }
  for (const k of Object.keys(fields)) {
    args.push(k);
    args.push(String(fields[k]));
  }
  return args;
}

/** redis.pipeline(): commands queued, then sent by exec(). */
export class Pipeline {
  private readonly client: Redis;
  private readonly commands: string[][] = [];

  constructor(client: Redis) {
    this.client = client;
  }

  private add(args: string[]): Pipeline {
    this.commands.push(args);
    return this;
  }

  get(key: string): Pipeline {
    return this.add(["GET", key]);
  }
  set(key: string, value: string | number, mode: string = "", time: number = -1, flag: string = ""): Pipeline {
    const args: string[] = ["SET", key, String(value)];
    if (mode.length > 0) {
      args.push(mode);
      if (time >= 0) {
        args.push(String(time));
      }
    }
    if (flag.length > 0) {
      args.push(flag);
    }
    return this.add(args);
  }
  del(...keys: string[]): Pipeline {
    return this.add(join(["DEL"], keys));
  }
  expire(key: string, seconds: number): Pipeline {
    return this.add(["EXPIRE", key, String(seconds)]);
  }
  incr(key: string): Pipeline {
    return this.add(["INCR", key]);
  }
  hget(key: string, field: string): Pipeline {
    return this.add(["HGET", key, field]);
  }
  hset(key: string, fields: string | Record<string, string | number>, value: string | number = ""): Pipeline {
    return this.add(hsetArgs(key, fields, value));
  }
  hmget(key: string, ...fields: string[]): Pipeline {
    return this.add(join(["HMGET", key], fields));
  }
  hgetall(key: string): Pipeline {
    return this.add(["HGETALL", key]);
  }
  sadd(key: string, ...members: (string | number)[]): Pipeline {
    return this.add(join(["SADD", key], strings(members)));
  }
  srem(key: string, ...members: (string | number)[]): Pipeline {
    return this.add(join(["SREM", key], strings(members)));
  }
  zadd(key: string, ...scoreMembers: (string | number)[]): Pipeline {
    return this.add(join(["ZADD", key], strings(scoreMembers)));
  }
  zrem(key: string, ...members: string[]): Pipeline {
    return this.add(join(["ZREM", key], members));
  }
  zremrangebyscore(key: string, min: number | string, max: number | string): Pipeline {
    return this.add(["ZREMRANGEBYSCORE", key, String(min), String(max)]);
  }

  /** Runs the commands in order: [null, reply] each, or [ReplyError, null] for one the server refused. */
  async exec(): Promise<[Error | null, unknown][] | null> {
    const out: [Error | null, unknown][] = [];
    for (const c of this.commands) {
      out.push(await this.client.__pipelined(c));
    }
    return out;
  }
}

export default Redis;
