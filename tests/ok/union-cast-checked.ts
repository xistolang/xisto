// unknown as A[] | A | null: each member checked as it is made (Array.isArray(v) ? v as A[] : v as A); a service reading its DataSource through get db(): { query<T = Row>(...) } and casting the rows
type Row = Record<string, unknown>;
interface R1 { N: number }
function conv(u: unknown): number {
  const r = u as R1[] | R1 | null | undefined;
  if (Array.isArray(r)) return r.length * 100;
  return r == null ? -1 : r.N;
}
const a: R1[] = [{ N: 1 }, { N: 2 }];
const b: R1 = { N: 7 };
console.log(conv(a), conv(b), conv(null));

class DS {
  async query<T = any>(sql: string, binds: unknown[] = []): Promise<T> {
    const raw: object = JSON.parse(sql === "one" ? '[{"N":1,"D":null}]' : "[]");
    const rows: T = raw as T;
    return rows;
  }
}
class Pk {
  private readonly dataSource: DS;
  constructor(ds: DS) { this.dataSource = ds; }
  private get db(): { query<T = Row>(sql: string, binds?: unknown[]): Promise<T> } {
    return this.dataSource as unknown as { query<T = Row>(sql: string, binds?: unknown[]): Promise<T> };
  }
  async rows<T = Row>(sql: string, binds: unknown[] = []): Promise<T[]> {
    const r = (await this.db.query(sql, binds)) as T[] | T | null | undefined;
    if (Array.isArray(r)) return r;
    return r == null ? [] : [r];
  }
}
async function main(): Promise<void> {
  const xs = await new Pk(new DS()).rows<{ N: number; D: string | null }>("one", [1]);
  console.log(xs.length, xs[0].N, xs[0].D);
}
main();
