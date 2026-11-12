// core boot (round 11): an interface of optional fields used as a view of an open class (getRequest<AuditableRequest>()):
// the request itself (a write through it seen by the next read); a view whose fields the class has, and an any as an
// interface of data, read by name; an int where number | null goes
// build: --tsc-checked
class Request {
  method: string = 'GET';
  url: string = '/';
  ip: string = '';
  user: any = null;
  statusCode: number = 200;
  __expando: Record<string, any> = {};
}
class Ctx {
  private request: Request = new Request();
  getRequest<T = Request>(): T {
    return this.request as unknown as T;
  }
  setUser(u: any): void {
    this.request.user = u;
  }
}
const AUDIT = '__audit';
interface AuditableRequest {
  __audit?: { action: string };
  ip?: string;
  url?: string;
  method?: string;
}
interface JwtPayload {
  sub: string;
  roles?: string[];
}
const ctx = new Ctx();
const req = ctx.getRequest<AuditableRequest>();
console.log(req.method, req.url, req.__audit === undefined);
req[AUDIT] = { action: 'x' };
const again = ctx.getRequest<AuditableRequest>();
console.log(again.__audit?.action, again === req);
ctx.setUser({ sub: 'u1', roles: ['admin', 'x'] });
const user = ctx.getRequest<{ user?: JwtPayload }>().user;
console.log(user?.sub, user?.roles?.includes('admin'));
const a3: any = { sub: 'u3', roles: ['r'] };
const u3 = a3 as JwtPayload;
console.log(u3.sub, u3.roles?.length);
const code: number = 7;
const st = { status: code as number | null };
console.log(st.status);
