// a call of a function that returns never ends the path; .catch((err) => { ...; return null; }) gives T | null; String(err) of an unknown Error
function f(e: unknown): string { return String(e); }
try { throw new Error("down"); } catch (e) { console.log(String(e), f(e)); }
const u: unknown = new Error("x");
console.log(String(u));
class Repo {
  async get(k: string): Promise<string | null> { if (k === "bad") throw new Error("down"); return k === "x" ? "val" : null; }
}
class Svc {
  private fail(e: unknown): never { throw new Error("mapped: " + String(e)); }
  async load(k: string): Promise<{ v: string }> {
    try {
      const s = await new Repo().get(k);
      return { v: s ?? "none" };
    } catch (error: unknown) {
      this.fail(error);
    }
  }
  async safe(k: string): Promise<string> {
    const s = await new Repo().get(k).catch((err) => {
      console.log("warn " + String(err));
      return null;
    });
    return s ?? "fallback";
  }
}
async function main(): Promise<void> {
  const s = new Svc();
  console.log((await s.load("x")).v, (await s.load("y")).v);
  try { await s.load("bad"); } catch (e) { console.log((e as Error).message); }
  console.log(await s.safe("x"), await s.safe("bad"));
}
main();
