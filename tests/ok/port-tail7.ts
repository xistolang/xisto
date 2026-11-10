// warns: XS2105
// appa round 7: promises of different types in one cache (Promise<unknown> converted both ways), handlers
// that always throw, .catch(() => 0) typed by the promise, then/catch as a statement with handlers of other
// types, Promise.race of work and a symbol timeout (an unknown), unknown === an object; arrays where wider arrays
// go (string[] into (string | number)[], int[] into number[], into Record | unknown[]), a predicate returning a
// count, objects of different shapes in one array (as JSON)
function query(sql: string, params: Record<string, unknown> | unknown[]): number {
  return Array.isArray(params) ? params.length : Object.keys(params).length;
}
function inQuery(values: (string | number)[]): number { return values.length; }
function total(xs: number[]): number { return xs.reduce((a, b) => a + b, 0); }
function nested(ls: number[][]): number { return ls.length; }
interface Item { name: string; tags: string[] }
function arrays(): void {
  const binds: (string | number | null)[] = ['a', 1, null];
  console.log(query('x', binds), query('y', ['a', 'b']));
  const names: string[] = ['ana', 'bia'];
  console.log(inQuery(names));
  const ids: int[] = [1, 2, 3];
  console.log(total(ids), nested([ids, ids]));
  const all: number[][] = [];
  all.push(ids.slice(0, 2));
  console.log(all);
  const items: Item[] = [{ name: 'a', tags: [] }, { name: 'b', tags: ['x'] }];
  console.log(items.filter((i) => i.tags.length).map((i) => i.name));
  const must = [{ term: { doc: 'r' } }, { range: { dia: { gte: '1', lte: '2' } } }, { term: { status: 'ok' } }];
  console.log(JSON.stringify({ query: { bool: { filter: must } } }));
}
class Cache {
  private readonly items = new Map<string, { at: number; value: Promise<unknown> }>();
  remember<T>(key: string, fn: () => Promise<T>): Promise<T> {
    const cur = this.items.get(key);
    if (cur) return cur.value as Promise<T>;
    const value = fn().then(
      (v) => v,
      (err: unknown) => {
        this.items.delete(key);
        throw err;
      },
    );
    this.items.set(key, { at: 1, value });
    return value;
  }
}
const TIMEOUT = Symbol('timeout');
async function slow(n: number): Promise<number> { await new Promise<void>((ok) => setTimeout(ok, n)); return n * 2; }
async function fails(): Promise<string[]> { throw new Error('boom'); }
async function nums(): Promise<number[]> { return [1, 2, 3]; }
async function name(): Promise<string> { return 'ana'; }
class Svc {
  private cache = new Map<string, Promise<string[]>>();
  private last = new Map<string, string[]>();
  load(k: string): Promise<string[]> {
    const p = fails();
    this.cache.set(k, p);
    p.then(
      (v) => this.last.set(k, v),
      () => this.cache.delete(k),
    );
    return p;
  }
  size(): number { return this.cache.size; }
}
async function main(): Promise<void> {
  arrays();
  const c = new Cache();
  const a = await c.remember('n', nums);
  const b = await c.remember('n', nums);
  const s = await c.remember('s', name);
  console.log(a, b, s);
  const p: Promise<unknown> = name();
  const q = p as Promise<string>;
  console.log((await q).length);
  const n = await slow(1).then((x) => x + 1).catch(() => 0);
  console.log('n', n);
  const z = await fails().then((xs) => xs.length).catch(() => 0);
  console.log('z', z);
  const svc = new Svc();
  try { await svc.load('a'); } catch (e) { console.log('caught', (e as Error).message); }
  console.log('cache', svc.size());
  const kept = slow(50);
  const timer = new Promise<typeof TIMEOUT>((ok) => setTimeout(() => ok(TIMEOUT), 5));
  const r = await Promise.race([kept, timer]);
  console.log('race', r === TIMEOUT ? 'timeout' : r);
  const r2 = await Promise.race([slow(1), new Promise<typeof TIMEOUT>((ok) => setTimeout(() => ok(TIMEOUT), 40))]);
  console.log('race2', r2 === TIMEOUT ? 'timeout' : r2);
  const v = slow(2).catch((err: unknown) => {
    console.log('never');
    throw err;
  });
  console.log('v', await v);
  void fails().catch(() => null);
  await kept;
  await flight();
}
// a const read by a closure in its own initializer (its type written), and a placeholder cast
class Flight {
  private inFlight: Promise<boolean> | null = null;
  private entry = { at: 0, value: null as unknown as Promise<number> };
  private async health(): Promise<boolean> { return true; }
  ok(): Promise<boolean> {
    if (this.inFlight) return this.inFlight;
    const reading: Promise<boolean> = this.health().finally(() => {
      if (this.inFlight === reading) this.inFlight = null;
    });
    this.inFlight = reading;
    return reading;
  }
  pending(): boolean { return this.inFlight !== null; }
  async value(): Promise<number> {
    this.entry.value = Promise.resolve(7);
    return await this.entry.value;
  }
}
async function flight(): Promise<void> {
  const f = new Flight();
  const p = f.ok();
  console.log('pending', f.pending(), await p, await f.value());
  await new Promise<void>((r) => setTimeout(r, 1));
  console.log('pending', f.pending());
}
main();
