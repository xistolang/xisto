class Client {
  name: string;
  constructor(name: string) {
    this.name = name;
  }
  async close(): Promise<string> {
    return `closed ${this.name}`;
  }
}
class Sink {
  client: Client | null = null;
  constructor(c: Client | null) {
    this.client = c;
  }
  async stop(): Promise<string> {
    if (!this.client) return "none";
    const teto = new Promise<string>((resolve) => setTimeout(() => resolve("timeout"), 1000));
    const r = await Promise.race([this.client.close().then((s) => s + "!"), teto]);
    return r;
  }
}
async function main(): Promise<void> {
  console.log(await new Sink(new Client("es")).stop());
  console.log(await new Sink(null).stop());
}
main();
