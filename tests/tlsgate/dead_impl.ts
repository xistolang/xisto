// an interface's other implementation fetches, but no object of it is ever made: no TLS (XB-232)
interface Exporter {
  send(body: string): Promise<int>;
}
class ConsoleExporter implements Exporter {
  async send(body: string): Promise<int> { console.log("send", body); return 0; }
}
class HttpExporter implements Exporter {
  url: string;
  constructor(url: string) { this.url = url; }
  async send(body: string): Promise<int> {
    const r = await fetch(this.url, { method: "POST", body: body });
    return r.status;
  }
}
function unused(url: string): Promise<Response> { return fetch(url); }
async function flush(e: Exporter): Promise<void> { console.log(await e.send("x")); }
await flush(new ConsoleExporter());
