// forms from a ported Nest app: number || default, process.env.NAME, an override that takes fewer parameters,
// T | Promise<T> as a return type, object types written in place with the same fields in base and override
class Base {
  audit(rule: string, n: number): Promise<void> {
    console.log("base audit", rule, n);
    return Promise.resolve();
  }
  can(x: number): boolean | Promise<boolean> {
    return x > 1;
  }
  shape(): { ok: boolean; n: number | null } {
    return { ok: true, n: 1 };
  }
}
class Quiet extends Base {
  override audit(): Promise<void> {
    console.log("quiet audit");
    return Promise.resolve();
  }
  override can(x: number): boolean | Promise<boolean> {
    return x > 5;
  }
  override shape(): { ok: boolean; n: number | null } {
    return { ok: false, n: null };
  }
}
async function main(): Promise<void> {
  const port = parseInt(process.env.XISTO_TEST_NO_SUCH_PORT ?? "", 10) || 3001;
  const zero = 0;
  console.log(port, zero || 7, 2.5 || 9);
  console.log(process.env.XISTO_TEST_NO_SUCH_NAME ?? "unset");
  const items: Base[] = [new Base(), new Quiet()];
  for (const b of items) {
    await b.audit("r", 2);
    console.log(await b.can(3), b.shape().ok);
  }
}
main();
