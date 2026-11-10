// strings ordered with < and >, a never-null value compared with null, int and number in one ?:
const a = 'abc', b = 'abd';
console.log(a < b, a > b, a <= 'abc', 'Z' < 'a', '' < 'a');
const s: string = 'x';
console.log(s === null, s !== null, s === undefined);
const pick = (n: number): number | null => (Number.isFinite(n) ? Math.trunc(n) : null);
console.log(pick(2.7), pick(Infinity));
// truthiness of values that are not names: !xs?.length, f() && ...
interface Box { items: string[] | null; }
const boxes: Box[] = [{ items: null }, { items: [] }, { items: ['a'] }];
for (const x of boxes) console.log(!x.items?.length, x.items?.length ? 'some' : 'none');
const cnt = (k: number): number | null => (k === 2 ? null : k === 3 ? NaN : k);
for (const k of [0, 1, 2, 3]) { const c = cnt(k); console.log(c ? 'truthy' : 'falsy', !c); }
// ...(c ? { k: v } : {}) into an object with optional fields
interface Evt { kind: string; ms?: number; tag?: string; }
const mk = (kind: string, ms: number | null, tag: string): Evt => ({ kind, ...(ms !== null ? { ms } : {}), ...(tag ? { tag } : {}) });
const e1 = mk('a', 5, ''), e2 = mk('b', null, 't');
console.log(e1.kind, e1.ms, e1.tag, e2.kind, e2.ms, e2.tag);
