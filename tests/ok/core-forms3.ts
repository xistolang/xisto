// round 7b: a () => number kept as an unknown and called as () => unknown (default: Date.now), objects of a class where
// an array of { toString(): string } goes, a sync (or async) function where (...args) => T | Promise<T> goes (useFactory)
// build: --tsc-checked
interface Opt { type: string; default?: unknown; }
function valor(o: Opt): unknown {
  const d = o.default;
  return typeof d === 'function' ? (d as () => unknown)() : d;
}
const t = valor({ type: 'Date', default: Date.now });
console.log(typeof t, (t as number) > 0);
const f: () => unknown = Date.now;
console.log(typeof f());
const g: () => unknown = () => 42;
console.log(g());
function n(): number { return 7; }
const h: () => unknown = n;
console.log(h());
class HostAddress {
  host: string; port: number;
  constructor(h: string, p: number) { this.host = h; this.port = p; }
  toString(): string { return this.host + ':' + this.port; }
}
function fmt(hosts: { toString(): string }[]): string { return hosts.map((h) => h.toString()).join(','); }
function one(h: { toString(): string }): string { return h.toString(); }
const hs = [new HostAddress('a', 1), new HostAddress('b', 2)];
console.log(fmt(hs), one(hs[0]));
interface Cfg { uri: string; }
interface Opts<T> { useFactory: (...args: unknown[]) => T | Promise<T>; inject?: unknown[]; }
async function forRootAsync(o: Opts<Cfg>): Promise<string> { const c = await o.useFactory(); return c.uri; }
function getMongoConfig(): Cfg { return { uri: 'mongodb://x' }; }
async function getAsync(): Promise<Cfg> { return { uri: 'mongodb://y' }; }
async function main(): Promise<void> {
  console.log(await forRootAsync({ useFactory: getMongoConfig }));
  console.log(await forRootAsync({ useFactory: getAsync }));
  console.log(await forRootAsync({ useFactory: () => ({ uri: 'z' }) }));
}
await main();
