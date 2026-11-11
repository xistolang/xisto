// forms found by the package front (kiosk): spread of an awaited value, a definite field (client!: T) built with new,
// err || !Array.isArray(v) as a test, new Error() with no message, literals whose later element throws (no leak),
// an object as Record<string, unknown> through unknown, JSON arrays cast to number[] / boolean[]
interface Jwk { kty: string; n: string; e: string; kid?: string }
async function exportJWK(k: string): Promise<Jwk> { return { kty: 'RSA', n: k, e: 'AQAB' }; }
async function principal(): Promise<void> {
  const kid = 'k1';
  const jwk: Jwk = { ...(await exportJWK('abc')), kid };
  console.log(JSON.stringify(jwk));
}
class Redis { ping(): string { return 'PONG'; } }
class Svc {
  client!: Redis;
  init(): void { this.client = new Redis(); }
}
const sv = new Svc(); sv.init(); console.log(sv.client.ping());
function testa(err: Error | null, v: unknown): string {
  if (err || !Array.isArray(v)) return 'bad';
  return 'ok ' + v.length;
}
console.log(testa(null, [1, 2]), testa(new Error('x'), []), testa(null, 3));
try { throw new Error(); } catch (e) { console.log((e as Error).message === '', (e as Error).name); }
class P { a: string = 'x'; }
function f(): P { throw new Error('boom'); }
function g(): (P | null)[] { return [null, f()]; }
function h(): P[] { return [new P(), f()]; }
function k(): string[] { return ['a' + Math.random(), f().a]; }
try { g(); } catch (e) { console.log('caught', (e as Error).message); }
try { h(); } catch (e) { console.log('caught', (e as Error).message); }
try { k(); } catch (e) { console.log('caught', (e as Error).message); }
const keep = new P();
function m(): P[] { return [keep, f()]; }
try { m(); } catch (e) { console.log('caught', (e as Error).message); }
console.log(keep.a);
class E { nome: string = 'n'; extra: Record<string, unknown> = { a: 1, b: 'x' }; }
const ee = new E();
const r = ee as unknown as Record<string, unknown>;
console.log(Object.keys(r).join(','), JSON.stringify(r));
const payload: Record<string, unknown> = JSON.parse('{"cands":[1,2,3.5],"ids":[4,5],"fl":[true,false],"s":"x"}');
const c = payload['cands'] as number[];
console.log(c.length, c[2], c.reduce((a, b) => a + b, 0));
const flags = payload['fl'] as boolean[];
console.log(flags.length, flags[0], (payload['ids'] as number[]).join('-'));
const u: unknown = [1, 2];
const back = u as number[];
console.log(back.length);
principal();
