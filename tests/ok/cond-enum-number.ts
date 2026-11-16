// '?:' with a number on one side and a member of a numeric enum on the other is a number (a string enum's member and a
// string: a string), as in TypeScript (it was XS8143: the two branches must have the same type)
enum HttpStatus { OK = 200, NOT_FOUND = 404, INTERNAL_SERVER_ERROR = 500 }
enum Kind { A = "a", B = "b" }
class HttpException { constructor(private s: number) {} getStatus(): number { return this.s; } }
function code(e: unknown): number {
  const status = e instanceof HttpException ? e.getStatus() : HttpStatus.INTERNAL_SERVER_ERROR;
  return status;
}
function code2(ok: boolean, n: number | null): number {
  return ok ? HttpStatus.OK : n ?? HttpStatus.NOT_FOUND;
}
function label(k: Kind | null, s: string): string {
  const v = k === null ? s : k;
  const w = k !== null ? k : `x${s}`;
  return v + "/" + w;
}
console.log(code(new HttpException(404)), code(new Error("x")), code2(true, null), code2(false, null), code2(false, 7));
console.log(label(Kind.B, "q"), label(null, "q"));
const xs: number[] = [1, 2];
const pick = xs.length > 1 ? xs[1] : HttpStatus.OK;
console.log(pick + 0.5);
