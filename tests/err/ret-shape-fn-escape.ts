// expect: and 'f' of guardaF lets it out: an adapter would not be the same function
interface Par { a: string; n: number; }
function par(f: (s: string) => Par): string {
  const p = f("x");
  return p.a + p.n;
}
const mk = (s: string) => ({ a: s + "!", n: s.length });
console.log(par(mk));
const guarda: ((s: string) => Par)[] = [];
function guardaF(f: (s: string) => Par): void { guarda.push(f); }
guardaF(mk);
