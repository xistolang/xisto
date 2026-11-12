// a generic arrow written inside a function with its result type left out, called without type arguments: what it
// returns is a parameter or a part of it (filter, slice, a ?: of those, null), so its result is that parameter's type
// with T inferred from the arguments (TypeScript's T[] | null), the same array when it is the parameter itself
// build: --tsc-checked
interface P { empresa: string; n: number; }
function filtros(emp: string | undefined): void {
  const ps: P[] | null = [{ empresa: "A", n: 1 }, { empresa: "B", n: 2 }];
  const filtra = <T extends { empresa: string }>(xs: T[] | null) => (xs == null || !emp ? xs : xs.filter((x) => x.empresa === emp));
  const r: P[] | null = filtra(ps);
  console.log(r, filtra(null), r === ps);
  const o = { lista: filtra(ps) };
  console.log(o.lista![0].n);
  const xs = ["a", "bb"];
  const ou = <T>(v: T[], c: boolean) => (c ? v.slice(1) : null);
  const r2 = ou(xs, true);
  console.log(r2, ou(xs, false), r2 === xs, r2![0].length);
}
filtros("B");
filtros(undefined);
