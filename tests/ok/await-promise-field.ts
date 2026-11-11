// a promise awaited through a field, while the .catch it carries clears that field: the awaiting task must keep it
// build: --tsc-checked
interface Estado {
  n: number;
}
class Servico {
  cache: { em: number; valor: Promise<Estado> } | null = null;
  falhas = 0;

  async mede(): Promise<Estado> {
    await new Promise<void>((r) => setTimeout(r, 2));
    this.falhas++;
    throw new Error(`banco fora (${this.falhas})`);
  }

  async estado(): Promise<Estado> {
    if (!this.cache || Date.now() - this.cache.em >= 60000) {
      const valor: Promise<Estado> = this.mede().catch((err: unknown) => {
        if (this.cache?.valor === valor) this.cache = null;
        throw err;
      });
      this.cache = { em: Date.now(), valor };
    }
    const base = await this.cache.valor;
    return { n: base.n + 1 };
  }
}

async function main(): Promise<void> {
  const s = new Servico();
  for (let i = 0; i < 3; i++) {
    try {
      const e = await s.estado();
      console.log("ok", e.n);
    } catch (err) {
      console.log("erro:", (err as Error).message, "cache limpo:", s.cache === null);
    }
  }
}
main();
