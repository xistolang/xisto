// expect: 'query' of LocalRequest is a method of Request, which LocalRequest is a view of
// build: --tsc-checked
class Request { method: string = 'GET'; __expando: Record<string, any> = {}; query(n: string): string { return n; } }
class Ctx { private r: Request = new Request(); getRequest<T = Request>(): T { return this.r as unknown as T; } }
interface LocalRequest { method: string; query?: Record<string, string>; }
const q = new Ctx().getRequest<LocalRequest>().query;
console.log(q);
