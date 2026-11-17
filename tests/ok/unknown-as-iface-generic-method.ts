// unknown as I (an injector's construct(args)): a class whose generic method has I's method name fits I only when its
// parameters that do not name the method's T take what I's method passes. A driver's execute<T>(sql: string, ...) is not
// a contract's execute(input: I): no adapter for it (whose call of execute(input) would not compile); a query<T>(sql)
// where the interface's query(sql) goes still fits.
interface ObjectContract<I, O> {
  execute(input: I): Promise<O>;
}
interface PlanIn { nu: string }
interface PlanOut { cd: number }
type PlanContract = ObjectContract<PlanIn, PlanOut>;
interface Queryable {
  query(sql: string): Promise<number>;
}

class Conn {
  async execute<T = number>(sql: string, binds: unknown[] = []): Promise<T[]> {
    console.log("conn.execute", sql, binds.length);
    return [];
  }
  async query<T = number>(sql: string): Promise<number> {
    return sql.length;
  }
}
class PlanImpl implements PlanContract {
  async execute(input: PlanIn): Promise<PlanOut> {
    return { cd: input.nu.length };
  }
}
class User {
  plan: PlanContract;
  constructor(plan: PlanContract) {
    this.plan = plan;
  }
  async run(nu: string): Promise<number> {
    return (await this.plan.execute({ nu })).cd;
  }
}
async function main(): Promise<void> {
  const made: unknown = new PlanImpl();
  const u = new User(made as PlanContract);
  console.log("plan", await u.run("abcde"));
  const conn = new Conn();
  const rows = await conn.execute("select 1 from dual", [1, 2]);
  console.log("rows", rows.length);
  const q: unknown = conn;
  console.log("query", await (q as Queryable).query("select 2"));
}
main();
