// methods through casts to an object type: (r as { code?: () => string })?.code?.() calls r's own code() (undefined when its class has none); (s as { valueOf(): string }).valueOf() on a string; for await over an iterable object
class Ecg {
  private readonly c: string;
  constructor(c: string) { this.c = c; }
  code(): string { return this.c; }
}
class Other { n = 1; }
function ok(r: Ecg | null): boolean {
  return (r as { code?: () => string })?.code?.() === "OK";
}
function ok2(r: Other | null): boolean {
  return (r as { code?: () => string })?.code?.() === "OK";
}
console.log(ok(new Ecg("OK")), ok(new Ecg("E1")), ok(null), ok2(new Other()), ok2(null));
function normaliza(densidade: string | null): string {
  const ehN = (densidade as { valueOf(): string }).valueOf() === "N";
  return ehN ? "D13" : (densidade as string);
}
console.log(normaliza("N"), normaliza("X"));
try { normaliza(null); } catch (e) { console.log("throws", e instanceof Error); }
class Req {
  rawBody: Buffer | null = Buffer.from("héllo");
  *[Symbol.iterator](): Generator<unknown> {
    const b = this.rawBody;
    if (b !== null) yield b;
  }
}
async function readRawBody2(request: Req, limit: number): Promise<Buffer | null> {
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of request) {
    const buffer = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk as string);
    size += buffer.byteLength;
    if (size <= limit) chunks.push(buffer);
  }
  return size > limit ? null : Buffer.concat(chunks);
}
async function main2(): Promise<void> {
  const r = await readRawBody2(new Req(), 100);
  console.log(r === null ? "null" : r.toString("utf8"), (await readRawBody2(new Req(), 2)) === null);
}
main2();
