interface L { linhas: number; vl: number | null; d: string | null }
const xs: L[] = [{ linhas: 2, vl: 1.5, d: "2024-01-02" }, { linhas: 3, vl: null, d: null }, { linhas: 5, vl: 2, d: "2024-03-01" }];
const total = xs.reduce((s, x) => s + x.linhas, 0);
const vl = xs.reduce((acc, z) => acc + (z.vl ?? 0), 0);
const maior = xs.map((x) => x.d).filter((d) => d !== null).reduce<string | null>((m, d) => (d != null && (m == null || d > m) ? d : m), null);
const soma = [1, 2, 3, 4].reduce((a, b) => a + b);
const fm = xs.flatMap((x) => [x.linhas, x.linhas * 10]);
const nested: string[][] = [["a", "b"], ["c"]];
const fl = nested.flat();
const idx = ["x", "y"].reduce((acc, s, i) => acc + s + String(i), "");
const ds = xs.map((x) => x.d);
console.log(ds.length, ds[1], ds[2]);
console.log(total, vl, maior, soma, fm.join(","), fl.join("|"), idx);
