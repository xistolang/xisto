// (module of tests/ok/fold-safety.ts: a function exported and called from the other module, with a literal)
export function scale(kind: string, n: number): number {
  if (kind === "double") return n * 2;
  if (kind === "half") return n / 2;
  return n;
}
