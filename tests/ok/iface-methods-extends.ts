// an interface of methods extending another (interface AptoDs extends ParecerDs): its abstract class extends the other's
interface ParecerDs {
  parecerPendente(cd: string): Promise<boolean>;
}
interface AptoDs extends ParecerDs {
  idade(cd: string): Promise<number>;
}
class BancoFake implements AptoDs {
  async parecerPendente(cd: string): Promise<boolean> { return cd === 'p'; }
  async idade(cd: string): Promise<number> { return cd.length * 10; }
}
async function apto(ds: AptoDs, cd: string): Promise<string> {
  const p = await ds.parecerPendente(cd);
  const i = await ds.idade(cd);
  return `${cd}: ${p ? 'pendente' : 'ok'} ${i}`;
}
async function soParecer(ds: ParecerDs, cd: string): Promise<boolean> { return ds.parecerPendente(cd); }
async function main(): Promise<void> {
  const b = new BancoFake();
  console.log(await apto(b, 'p'), await apto(b, 'abc'), await soParecer(b, 'p'));
}
main();
