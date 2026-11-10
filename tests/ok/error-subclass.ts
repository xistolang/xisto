// class X extends Error: own fields, super(message), name, thrown and caught, instanceof (and !instanceof) narrowing the caught Error, String(e)
class QueryFailedError extends Error {
  readonly driverError: { code: string; errno: number };
  constructor(query: string, code: string) {
    super(`query failed: ${query}`);
    this.name = "QueryFailedError";
    this.driverError = { code: code, errno: 1062 };
  }
}
class Plain extends Error {}
class Named extends Error {
  name = "Named";
  status: number = 500;
}
interface Meta { statusCode: number; body: string }
class ResponseError extends Error {
  meta: Meta;
  constructor(meta: Meta) {
    super("response error");
    this.name = "ResponseError";
    this.meta = meta;
  }
  get statusCode(): number { return this.meta.statusCode; }
}
class NotFound extends ResponseError {
  constructor() { super({ statusCode: 404, body: "nf" }); }
}
function deep(n: number): string {
  if (n === 0) throw new QueryFailedError("insert", "ER_DUP_ENTRY");
  if (n === 1) throw new Plain("plain one");
  if (n === 2) throw new Named("named");
  if (n === 3) throw new NotFound();
  if (n === 4) throw new ResponseError({ statusCode: 503, body: "busy" });
  if (n === 5) throw new Error("base");
  return "ok";
}
function mid(n: number): string { return deep(n) + "!"; }
for (let n = 0; n <= 6; n++) {
  try {
    console.log(mid(n));
  } catch (e) {
    console.log(n, e.name, e.message, e instanceof Error);
    if (e instanceof QueryFailedError) console.log("dup", e.driverError.code, e.driverError.errno);
    if (e instanceof Named) console.log("status", e.status);
    if (e instanceof ResponseError) console.log("resp", e.meta.statusCode, e.statusCode, e instanceof NotFound);
    const code = e instanceof ResponseError ? e.meta.statusCode : 0;
    console.log("code", code, e instanceof ResponseError && e.meta.body === "nf");
  }
}
const p = new Plain();
console.log(p instanceof Error, p.message === "", p.name);
const q = new QueryFailedError("q", "c");
console.log(q.message, q.name, q.driverError.code, q instanceof QueryFailedError);
function rethrow(): void {
  try {
    deep(3);
  } catch (e) {
    if (e instanceof NotFound) throw e;
  }
}
try { rethrow(); } catch (e) {
  console.log("again", e.name, e.message, e instanceof NotFound, e instanceof ResponseError, e instanceof QueryFailedError);
}
class Desvio extends Error {
  codigo: number;
  constructor(codigo: number) { super("desvio " + String(codigo)); this.codigo = codigo; }
}
function f(n: number): number {
  if (n > 1) throw new Desvio(n);
  if (n === 1) throw new Error("db");
  return 0;
}
function g(n: number): number {
  try {
    return f(n);
  } catch (x) {
    if (!(x instanceof Desvio)) throw x;
    return x.codigo * 10;
  }
}
console.log(g(0), g(5));
try { g(1); } catch (e) { console.log("propagated", e.message, e instanceof Desvio); }
for (const n of [1, 2, 3]) {
  try { f(n); } catch (erro) {
    if (!(erro instanceof Desvio) || erro.codigo !== 3) console.log("not three", erro.message);
    else console.log("three", erro.codigo);
    const msg = erro instanceof Error ? erro.message : String(erro);
    console.log(msg, String(erro));
  }
}
console.log(String(new Plain()), String(new Named("")), String(new Desvio(9)));
