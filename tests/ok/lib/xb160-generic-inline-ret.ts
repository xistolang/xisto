export const group = <T>(xs: T[], key: (x: T) => string): { n: number; b: Record<string, T> } => {
  const b: Record<string, T> = {};
  for (const x of xs) b[key(x)] = x;
  return { n: xs.length, b };
};
export function split<T>(xs: T[]): { head: T | null; rest: T[] } { return { head: xs.length > 0 ? xs[0] : null, rest: xs.slice(1) }; }
export class Repo {
  page<T>(xs: T[], size: number): { items: T[]; total: number } { return { items: xs.slice(0, Math.trunc(size)), total: xs.length }; }
}
