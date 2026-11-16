// A class whose field has a network type (HttpRequest) in a program that runs no server: the struct must still be
// declared, so the network runtime comes in (XB-250 had left it out: the C did not compile).
class Ctx {
  req: HttpRequest | null = null;
  name = "ctx";
}
function describe(c: Ctx): string {
  return c.name + " " + (c.req === null ? "no request" : "with request");
}
console.log(describe(new Ctx()));
