// an object literal of functions where an interface of methods goes (and Pick of it): its methods call the functions
interface Deps {
  feriado(data: Date): Promise<number>;
  familiar(p: string | null): Promise<string | null>;
  outro(): number;
}
async function dias(deps: Pick<Deps, "feriado">, d: Date): Promise<number> {
  return (await deps.feriado(d)) + 1;
}
async function ramos(deps: Pick<Deps, "feriado" | "familiar">, d: Date): Promise<string> {
  const n = await dias(deps, d);
  const f = await deps.familiar("x");
  return `${n} ${f}`;
}
function full(): Deps {
  return {
    feriado: async () => 1,
    familiar: async (p: string | null) => p,
    outro: () => 3,
  };
}
async function main(): Promise<void> {
  console.log(await ramos({ feriado: async () => 0, familiar: async (p: string | null) => p + "!" }, new Date()));
  console.log(await ramos(full(), new Date()), full().outro());
}
interface Store {
  get(k: string): Promise<string | null>;
  put(k: string, v: string): void;
  size(): number;
}
function memory(): Store {
  const m = new Map<string, string>();
  return {
    get: async (k) => m.get(k) ?? null,
    put: (k, v) => { m.set(k, v); },
    size: () => m.size,
  };
}
async function main2(): Promise<void> {
  const s = memory();
  s.put("a", "1");
  console.log(await s.get("a"), await s.get("b"), s.size());
  const p: Pick<Store, "size"> = { size: () => 7 };
  console.log(p.size());
}
main().then(() => main2());
