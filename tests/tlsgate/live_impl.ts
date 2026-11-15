// the fetching implementation is made (behind a condition), and its subclass and a decoded class too: TLS
interface Exporter {
  send(body: string): Promise<int>;
}
class ConsoleExporter implements Exporter {
  async send(body: string): Promise<int> { console.log("send", body); return 0; }
}
class HttpExporter implements Exporter {
  url: string = "";
  async send(body: string): Promise<int> {
    const r = await fetch(this.url, { method: "POST", body: body });
    return r.status;
  }
}
async function flush(e: Exporter): Promise<void> { console.log(await e.send("x")); }
const e: Exporter = process.argv.length > 5 ? JSON.parse("{\"url\":\"x\"}") as HttpExporter : new ConsoleExporter();
await flush(e);
