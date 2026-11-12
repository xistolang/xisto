// a view of the request whose fields the class declares as any (user?: { usuario?: string }): typed as the view says,
// read from the same object; a property the class does not declare written and read back through it
// build: --tsc-checked
class Request {
  method: string = 'GET';
  url: string = '/';
  user: any = null;
  body: any = {};
  __expando: Record<string, any> = {};
  query(name: string): string { return 'q:' + name; }
}
class Ctx {
  private request: Request = new Request();
  getRequest<T = Request>(): T { return this.request as unknown as T; }
  setUser(u: any): void { this.request.user = u; }
}
interface LocalRequest {
  user?: { usuario?: string };
  method: string;
  body?: unknown;
  __audit?: { a: number };
}
const ctx = new Ctx();
ctx.setUser({ usuario: '  op1 ' });
const request = ctx.getRequest<LocalRequest>();
const operator = request.user?.usuario?.trim().toUpperCase();
console.log(operator, request.method.toUpperCase());
request.__audit = { a: 1 };
console.log(ctx.getRequest<LocalRequest>().__audit?.a, ctx.getRequest<LocalRequest>() === request);
const c2 = new Ctx();
console.log(c2.getRequest<LocalRequest>().user?.usuario ?? 'none');
