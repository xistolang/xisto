import { Injectable } from "@nestjs/common";
import { createClient } from "redis";
import { CACHE_TTL, REDIS_URL } from "../config";

const RETRY_MS = 30000;

// One Redis client for the application (node-redis on Node, packages/redis on xisto).
const client = createClient({ url: REDIS_URL, socket: { reconnectStrategy: false } });
client.on("error", (err: Error): void => {
  console.log(`[cache] ${err.message}`);
});

export interface Cached {
  key: string;
  gen: string;
  value: string | null; // the cached response; null on a miss
}

// A read-through cache of responses that are the same for every user (the article list, an article).
// Each entry is "<generation>\n<json>". Every write that changes what those responses show increments the
// generation (invalidate); an entry from another generation is a miss. One round trip per read: MGET of the
// generation and the entry. The generation is read before the database is, and the response is stored with it:
// a write that lands in between makes the entry a miss, never stale. Without Redis the cache is off (every read
// goes to the database) and connecting is tried again every 30 s.
@Injectable()
export class CacheService {
  private ready: boolean = false;
  private retryAt: number = 0;

  private async open(): Promise<boolean> {
    if (this.ready || Date.now() < this.retryAt) {
      return this.ready;
    }
    this.retryAt = Date.now() + RETRY_MS; // concurrent requests do not pile up connects meanwhile
    try {
      if (!client.isOpen) {
        await client.connect(); // node-redis closes the client when the socket drops: open it again
      }
      this.ready = true;
    } catch (e) {
      console.log(`[cache] off for now: ${e.message}`);
    }
    return this.ready;
  }

  // Redis failed: off until the next attempt (RETRY_MS)
  private down(): void {
    this.ready = false;
    this.retryAt = Date.now() + RETRY_MS;
  }

  // null when the cache is off
  async lookup(key: string): Promise<Cached | null> {
    if (!(await this.open())) {
      return null;
    }
    try {
      const full = `conduit:${key}`;
      const r = await client.mGet(["conduit:gen", full]);
      const g = r[0];
      const gen = g || "0";
      const raw = r[1];
      const prefix = `${gen}\n`;
      let value: string | null = null;
      if (raw && raw.startsWith(prefix)) {
        value = raw.slice(prefix.length);
      }
      return { key: full, gen: gen, value: value };
    } catch (e) {
      this.down();
      return null;
    }
  }

  async store(c: Cached | null, value: string): Promise<void> {
    if (c === null) {
      return;
    }
    try {
      await client.set(c.key, `${c.gen}\n${value}`, { EX: CACHE_TTL });
    } catch (e) {
      this.down();
      console.log(`[cache] write failed: ${e.message}`);
    }
  }

  async invalidate(): Promise<void> {
    if (!(await this.open())) {
      return;
    }
    try {
      await client.incr("conduit:gen");
    } catch (e) {
      this.down();
      console.log(`[cache] invalidate failed: ${e.message}`);
    }
  }
}
