// forms of the kiosk (round 6): an override with other parameters, filter by a predicate into an interface, o[k as keyof
// typeof o], m?.['k'] then m['k'], e.constructor.name, h ^= c on a number, c.toString(16), rows.map((r: T) => ...) on an any
// build: --tsc-checked
class Repo {
  upsert(filter: string, set: number, setOnInsert: boolean): string {
    return `base ${filter} ${set} ${setOnInsert}`;
  }
  run(): string { return this.upsert('f', 1, true); }
}
interface Input { id: string; n: number; }
class FleetRepo extends Repo {
  upsert(input: Input): string {
    return `fleet ${input.id} ${input.n}`;
  }
}
const r = new FleetRepo();
console.log(r.upsert({ id: 'x', n: 2 }));
const b: Repo = new Repo();
console.log(b.upsert('g', 3, false));
class Sub2 extends FleetRepo { upsert(input: Input): string { return 'sub2 ' + input.id; } }
const fr: FleetRepo = new Sub2();
console.log(fr.upsert({ id: 'y', n: 0 }));
console.log(new Repo().run());

interface FeedVersion { versao: string; publicadoEm: string | null; }
interface Feed { versoes: FeedVersion[]; }
const nomes = ['a-1.2', 'x', 'b-1.3'];
const versoes = nomes
  .map((n) => {
  const m = /-(\d+\.\d+)$/.exec(n);
  if (!m) return null;
  return { versao: m[1], publicadoEm: n.length > 4 ? 'now' : null };
})
  .filter((v): v is FeedVersion => v !== null)
  .sort((a, b) => b.versao.localeCompare(a.versao));
const f: Feed = { versoes };
console.log(f.versoes.length, f.versoes[0].versao, f.versoes[1].publicadoEm);

const COLS = { a: 'COL_A', b: 'COL_B', c: 'COL_C' } as const;
const UNCHECKED = { a: '0', c: "'N'" } as const;
type Field = keyof typeof COLS;
function order(sortBy?: Field): string {
  const field = sortBy && Object.prototype.hasOwnProperty.call(COLS, sortBy) ? sortBy : 'a';
  const column = COLS[field];
  const unchecked = Object.prototype.hasOwnProperty.call(UNCHECKED, field)
    ? UNCHECKED[field as keyof typeof UNCHECKED]
    : undefined;
  return `${column} ${unchecked ?? '-'}`;
}
console.log(order('c'), order('b'), order());
interface Patch { cd: number | null; ds?: string; fl?: string; }
const patch: Patch = { cd: 3, ds: '' };
const columns = { cd: 'CD', ds: 'DS', fl: 'FL' };
const values: unknown[] = [];
const sets = Object.entries(columns).flatMap(([key, column]) => {
  const value = patch[key as keyof typeof patch];
  if (value === undefined) return [];
  values.push(value === '' ? null : value);
  return [`${column} = :${values.length}`];
});
console.log(sets.join(', '), values.length);
function rec(n: number): Record<string, unknown> | null { return n > 0 ? { NU: 'x' + n } : null; }
const ans = rec(1), none = rec(0);
console.log(ans?.['NU'] ? String(ans['NU']) : '-', none?.['NU'] ? String(none['NU']) : '-');

class AppError extends Error { constructor(m: string) { super(m); } }
class DbError extends AppError {}
function kind(e: Error): string { return e.constructor.name; }
console.log(kind(new Error('a')), kind(new AppError('b')), kind(new DbError('c')));
try { decodeURIComponent('%'); } catch (err) { console.log(kind(err as Error)); }
class Pt { x = 1; }
const p = new Pt();
console.log(p.constructor.name);

function hashOnda(value: string): number {
  let h = 0x811c9dc5;
  for (let i = 0; i < value.length; i++) {
    h ^= value.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}
console.log(hashOnda('kiosk-01'), hashOnda(''));
const s = 'a\u0001b';
const hits: string[] = [];
for (let i = 0; i < s.length; i++) {
  const c = s.charCodeAt(i);
  if (c < 0x20) hits.push(`${i}:0x${c.toString(16).padStart(2, '0')}`);
}
console.log(hits.join(','));
async function query(sql: string): Promise<any> {
  return JSON.parse('[{"DT_EXAME":"2026-01-02T10:00:00.000Z"},{"DT_EXAME":"2026-03-04T00:00:00.000Z"}]');
}
function toDate(v: Date | string): Date { return v instanceof Date ? v : new Date(v); }
async function main(): Promise<void> {
  const rows = await query('select');
  const ds = rows.map((r: { DT_EXAME: Date | string }) => toDate(r.DT_EXAME));
  console.log(ds.length, ds[1].toISOString());
}
main();
