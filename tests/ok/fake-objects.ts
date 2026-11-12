// an object literal standing for a class or interface with methods (fakes: NOOP_CONTEXT as unknown as ExecutionContext,
// a CallHandler written in place): the methods it gives, TypeError for the others when called
// build: --tsc-checked
class Ctx {
  private req: unknown = null;
  constructor(r: unknown) { this.req = r; }
  getHandler(): () => unknown { return () => 1; }
  getClass(): unknown { return "Ctx"; }
  getType(): string { return 'http'; }
  switchToHttp(): { getRequest(): unknown } { return { getRequest: () => this.req }; }
}
function use(c: Ctx): string {
  const h = c.getHandler();
  return typeof h + ' ' + (h() === undefined ? 'undef' : 'val') + ' ' + (c.getClass() !== null);
}
const NOOP = {
  getHandler: () => () => undefined,
  getClass: () => class {},
} as unknown as Ctx;
console.log(use(NOOP));
try {
  NOOP.getType();
} catch (e) {
  console.log((e as Error).name, (e as Error).message.replace('NOOP.', ''));
}
interface Handler { handle(): string; }
const h: Handler = { handle: () => 'handled' };
console.log(h.handle());
const req: { url: string } = { url: '/x' };
const ctx2 = { switchToHttp: () => ({ getRequest: () => req }) } as unknown as Ctx;
console.log((ctx2.switchToHttp().getRequest() as { url: string }).url);
