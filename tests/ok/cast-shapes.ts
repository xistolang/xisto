class PaginationQueryDto { page: number = 1; limit: number = 10; nuOrdemUsuario: string = "7"; }
function ordem(query: PaginationQueryDto): string | null {
  const n = (query as { nuOrdemUsuario?: string }).nuOrdemUsuario;
  return n ?? null;
}
function deep(r: Record<string, unknown>): object { return r as object; }
const s = "abcdefgh";
const w = 5;
console.log(ordem(new PaginationQueryDto()), JSON.stringify(deep({ a: 1, b: "x" })), s.slice(0, w / 2), "7".padStart(w + 0.5, "0"), "ab".repeat(w / 2));
const t = setTimeout(() => console.log("timer"), 1);
t.unref?.();
setTimeout(() => console.log("done"), 30); // (a referenced timer keeps the process: the unref one fires before it, in both)
