class Leitura {
  tipo: "linha" | "x" = "linha";
  n: number = 1;
}
const o: any = new Leitura();
console.log(o.tipo, JSON.stringify(new Leitura()));
