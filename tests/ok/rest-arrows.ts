export const Roles = (...roles: string[]): string => roles.join("|");
console.log(Roles("a", "b", "c"), Roles());
function outer(): void {
  const sum = (base: number, ...xs: number[]): number => { let t = base; for (const x of xs) t += x; return t; };
  console.log(sum(1, 2, 3), sum(10));
}
outer();
interface Q { query: (...args: unknown[]) => string; }
const q: Q = { query: (...args: unknown[]): string => `${args.length}` };
console.log(q.query(1, "a", true), q.query());
type Log = (...parts: string[]) => void;
const log: Log = (...parts: string[]): void => console.log(parts.join(" "));
log("x", "y");
