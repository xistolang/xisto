// views of the request in @Request() params and getRequest<T>(): a field the class types otherwise read as the view
// says (user any as { sub: string }), one view passed where another view of the request goes (the same object)
// build: --tsc-checked
class Request {
  method: string = 'GET';
  user: any = null;
  headers: Record<string, string | string[]> = {};
  __expando: Record<string, any> = {};
}
class Ctx {
  private request: Request = new Request();
  getRequest<T = Request>(): T { return this.request as unknown as T; }
  setUp(): void { this.request.user = { sub: '7' }; this.request.headers['x-forwarded-for'] = '1.2.3.4, 5.6.7.8'; }
}
type ReqA = { user: { sub: string; tid?: string }; headers?: Record<string, string | string[]> };
type ReqB = { user: { sub: string } };
function realizar(req: ReqB): string { return 'sub ' + req.user.sub; }
const ctx = new Ctx();
ctx.setUp();
const req = ctx.getRequest<ReqA>();
const xff = req.headers?.['x-forwarded-for'];
const ip = (Array.isArray(xff) ? xff[0] : xff)?.split(',')[0]?.trim();
console.log(ip, Number(req.user.sub), realizar(req));
