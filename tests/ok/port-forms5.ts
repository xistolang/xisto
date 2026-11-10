// declare global, index signatures, defaulted type parameters of interface methods, typed arrow defaults
declare global {
  interface ErrorConstructor { new (message?: string): Error; }
}
interface Ctx { a: string; n?: number; [k: string]: unknown; }
interface Sessao { query<T = string>(sql: string): Promise<T>; }
class S implements Sessao { async query(sql: string): Promise<string> { return "r:" + sql; } }
const f = (x: string, y: number | null = null): string => `${x}${y}`;
async function main(): Promise<void> {
  const s: Sessao = new S();
  const c: Ctx = { a: "x" };
  console.log(await s.query("q"), c.a, f("a"), f("b", 2));
}
let tracer: any;
function lvl(env: NodeJS.ProcessEnv = process.env): string {
  return env.LOG_LEVEL ?? "info";
}
console.log(tracer == null, lvl());
main();
