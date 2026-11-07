// redis (node-redis v4): createClient({ url }), connect(), then await client.get/set/del/... as in Node.
// RESP2 over TCP in native/redis.c, with a connection pool per worker thread. Server errors are thrown Errors.
export interface RedisSocketOptions {
  host?: string;
  port?: int;
  reconnectStrategy?: boolean;
}

export interface RedisClientOptions {
  url?: string;
  username?: string;
  password?: string;
  database?: int;
  socket?: RedisSocketOptions;
}

export interface SetOptions {
  EX?: int;
  PX?: int;
  NX?: boolean;
  XX?: boolean;
}

declare class NativeRedis {
  constructor(config: object);
  connect(): void;
  str(args: string[]): string | null;
  int(args: string[]): int;
  strs(args: string[]): string[];
  strsn(args: string[]): (string | null)[];
  close(): void;
}

export class RedisClient {
  private readonly native: NativeRedis;
  isOpen: boolean = false;
  isReady: boolean = false;

  constructor(options: RedisClientOptions) {
    this.native = new NativeRedis(options);
  }

  // node-redis reports connection errors as events; here every command throws them, so listeners are not called
  on(event: string, listener: (err: Error) => void): RedisClient {
    return this;
  }

  async connect(): Promise<RedisClient> {
    this.native.connect();
    this.isOpen = true;
    this.isReady = true;
    return this;
  }

  async ping(): Promise<string> {
    const r = this.native.str(["PING"]);
    return r || "";
  }

  async get(key: string): Promise<string | null> {
    return this.native.str(["GET", key]);
  }

  async set(key: string, value: string, options?: SetOptions): Promise<string | null> {
    const args: string[] = ["SET", key, value];
    if (options !== null) {
      const ex = options.EX;
      const px = options.PX;
      if (ex !== null) {
        args.push("EX");
        args.push(`${ex}`);
      }
      if (px !== null) {
        args.push("PX");
        args.push(`${px}`);
      }
      if (options.NX === true) {
        args.push("NX");
      }
      if (options.XX === true) {
        args.push("XX");
      }
    }
    return this.native.str(args);
  }

  async del(key: string): Promise<number> {
    return Number(this.native.int(["DEL", key]));
  }

  async exists(key: string): Promise<number> {
    return Number(this.native.int(["EXISTS", key]));
  }

  async incr(key: string): Promise<number> {
    return Number(this.native.int(["INCR", key]));
  }

  async incrBy(key: string, by: int): Promise<number> {
    return Number(this.native.int(["INCRBY", key, `${by}`]));
  }

  async expire(key: string, seconds: int): Promise<boolean> {
    return this.native.int(["EXPIRE", key, `${seconds}`]) === 1;
  }

  async ttl(key: string): Promise<number> {
    return Number(this.native.int(["TTL", key]));
  }

  async mGet(keys: string[]): Promise<(string | null)[]> {
    const args: string[] = ["MGET"];
    for (const k of keys) {
      args.push(k);
    }
    return this.native.strsn(args);
  }

  async keys(pattern: string): Promise<string[]> {
    return this.native.strs(["KEYS", pattern]);
  }

  async flushDb(): Promise<string> {
    const r = this.native.str(["FLUSHDB"]);
    return r || "";
  }

  // any command, for what the typed methods do not cover: string replies (integers as text), or null
  async sendCommand(args: string[]): Promise<string | null> {
    return this.native.str(args);
  }

  async quit(): Promise<string> {
    this.native.close();
    this.isOpen = false;
    this.isReady = false;
    return "OK";
  }

  async disconnect(): Promise<void> {
    this.native.close();
    this.isOpen = false;
    this.isReady = false;
  }
}

export function createClient(options: RedisClientOptions = {}): RedisClient {
  return new RedisClient(options);
}
