// expect: the module of import() is read through its names (m.x), not as a value
async function load(): Promise<void> {
  const m = await import("../ok/lib/dyn-shared.ts");
  console.log(m);
}
load();
