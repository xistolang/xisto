// REST client as a TypeScript class. This exact file runs on Node, Bun and Xisto.
class ApiClient {
  base: string;
  calls: int = 0;
  constructor(base: string) {
    this.base = base;
  }
  async ping(): Promise<string> {
    this.calls++;
    const r: Response = await fetch(this.base + "/ping");
    return await r.text();
  }
  async hello(name: string): Promise<string> {
    this.calls++;
    const r: Response = await fetch(this.base + "/hello?name=" + name);
    return await r.text();
  }
}

async function main(): Promise<void> {
  const client: ApiClient = new ApiClient("http://127.0.0.1:8080");
  const n: int = 20000;
  let ok: int = 0;
  let bytes: int = 0;
  for (let i: int = 0; i < n; i++) {
    const pong: string = await client.ping();
    if (pong === "pong") {
      ok++;
    }
    const h: string = await client.hello("isa");
    bytes += h.length;
  }
  console.log("calls", client.calls, "ok", ok, "bytes", bytes);
}

main();
