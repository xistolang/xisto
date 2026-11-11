// any reads a field its value does not have as undefined (err?.status || 500), as JavaScript does
class HttpError extends Error {
  status: number;
  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}
class Row {
  id = 7;
  name = "x";
}
function statusOf(error: any): number {
  return error?.status || 500;
}
function describe(v: any): string {
  return `${v.name ?? "?"}/${v.code ?? "no code"}/${v.id ?? "no id"}`;
}
const plain: any = new Error("boom");
console.log(statusOf(new HttpError("nope", 404)), statusOf(plain), statusOf(null));
console.log(plain.message, plain.status === undefined, plain.status ?? "none");
console.log(describe(new Row()), describe(plain));
console.log(("abc" as any).length, ([1, 2] as any).length, (plain as any).name);
try {
  throw new HttpError("teapot", 418);
} catch (err) {
  const e: any = err;
  console.log(e.message, e.status, e.name, e.code ?? "no code", statusOf(err));
}
try {
  const n: any = null;
  console.log(n.x);
} catch (e) {
  console.log("null read:", e instanceof Error);
}
