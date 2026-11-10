class Memo {
  async lembra<T>(k: string, fn: () => Promise<T>): Promise<T> { return await fn(); }
}
class Repo { async one(id: number): Promise<string | null> { return id > 0 ? 'r' + String(id) : null; } }
class Svc {
  private memo = new Memo();
  private repo: Repo | null = new Repo();
  private leitura<T>(chave: string, fn: () => Promise<T> | T): Promise<T> {
    return this.memo.lembra(chave, async () => fn());
  }
  async run(): Promise<void> {
    const a = await this.leitura('a', () => this.repo?.one(1));
    console.log(a);
    const b = await this.leitura('b', () => 5);
    console.log(b);
  }
}
new Svc().run();
