// a prototype patched: Object.getPrototypeOf(runner).query = function wrapping the original (original.apply(this, args)); every call of Runner.query goes through it, as in JavaScript; an expando on the prototype (__patched)
class Runner {
  n = 0;
  async query(sql: string, binds: unknown[] = []): Promise<number> {
    this.n++;
    return sql.length + binds.length;
  }
  async release(): Promise<void> {}
}
class Source {
  createQueryRunner(): Runner {
    return new Runner();
  }
  async run(sql: string): Promise<number> {
    return this.createQueryRunner().query(sql, [1]);
  }
}

export function instala(ds: Source): boolean {
  const runner = ds.createQueryRunner();
  const proto = Object.getPrototypeOf(runner) as {
    query: (...args: unknown[]) => Promise<unknown>;
    __patched?: boolean;
  };
  void runner.release?.();
  if (proto.__patched) return true;
  const original = proto.query;
  proto.query = async function patched(this: unknown, ...args: unknown[]): Promise<unknown> {
    const a0 = args[0];
    const sql = typeof a0 === "string" ? a0 : "";
    if (sql.startsWith("DELETE")) throw new Error("blocked " + sql);
    const r = await original.apply(this, args);
    return (r as number) * 100;
  };
  proto.__patched = true;
  return true;
}

async function main(): Promise<void> {
  const ds = new Source();
  console.log(await ds.run("SELECT 1"));
  console.log(instala(ds), instala(ds));
  console.log(await ds.run("SELECT 1"));
  try { await ds.run("DELETE x"); } catch (e) { console.log((e as Error).message); }
  const r = ds.createQueryRunner();
  console.log(await r.query("ab"), r.n);
}
main();
