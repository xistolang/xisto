// readonly T[], Readonly<T> and ReadonlyArray<T>: the type itself (not writing to it is TypeScript's check)
interface Cfg { host: string; port: number }
function sum(xs: readonly number[]): number { let t = 0; for (const x of xs) t += x; return t; }
const c: Readonly<Cfg> = { host: "h", port: 1 };
const names: ReadonlyArray<string> = ["a", "b"];
const grid: readonly (readonly string[])[] = [["x"]];
console.log(sum([1, 2, 3]), c.host, c.port, names.length, grid[0][0]);
