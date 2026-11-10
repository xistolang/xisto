class MyErr extends Error { erros: string[] = ['x']; }
const msg = (e: unknown): string => (e instanceof Error ? e.message : String(e));
console.log(msg(new Error('boom')), msg('str'));
function errs(err: unknown): string[] | null {
  if (err instanceof MyErr) return err.erros;
  return null;
}
console.log(errs(new MyErr('m')), errs(1));
const fmt = (v: unknown): string => (v instanceof Date ? v.toISOString() : String(v));
console.log(fmt(new Date(0)), fmt(5));
const n1: number | string = '7';
console.log(Number(n1), String(n1));
const ns: string | null = null;
console.log(Number(ns), String(ns));
