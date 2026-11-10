// n.toLocaleString('pt-BR' | 'en-US', options written in place) and n.toPrecision(p), as Node prints them
const xs: number[] = [0, 1, 1234.5, -5, 1234567.891, 1.005, 0.1 + 0.2, 999.995, -0.004];
for (const v of xs) {
  console.log(v.toLocaleString('pt-BR'), v.toLocaleString('pt-BR', { style: 'currency', currency: 'BRL' }),
    v.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 }));
}
const n: int = 1234567;
console.log(n.toLocaleString('pt-BR'));
for (const v of [123.456, 0.000123, 1e21, 0, 5, 1234.5678, -0.5]) console.log(v.toPrecision(4), v.toPrecision(1));
console.log((-0).toLocaleString("pt-BR"), (0).toLocaleString("pt-BR"));
