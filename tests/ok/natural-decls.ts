// Declarations as TypeScript code writes them: `let x: T;` assigned later, timer handles typed NodeJS.Timeout,
// and generic functions written as arrows (const f = <T>(...) => ...).
const primeiro = <T>(xs: T[]): T | null => (xs.length > 0 ? xs[0] : null);
const emPares = <T,>(xs: T[]): T[][] => {
  const out: T[][] = [];
  for (let i = 0; i < xs.length; i += 2) out.push(xs.slice(i, i + 2));
  return out;
};
export const ultimo = <T>(xs: T[]) => xs[xs.length - 1];

class Relogio {
  private timer: NodeJS.Timeout | null = null;
  private outro: ReturnType<typeof setTimeout> | null = null;
  inicia(ms: number, msg: string): void {
    this.timer = setTimeout(() => console.log(msg), ms);
    this.outro = setTimeout(() => console.log("nunca"), ms * 100);
  }
  para(): void {
    if (this.outro !== null) clearTimeout(this.outro);
    this.timer = null;
  }
}

function classifica(n: number): string {
  let rotulo: string;
  let peso: number;
  let linhas: string[];
  let talvez: string | null;
  let ok: boolean;
  if (n > 10) {
    rotulo = "grande";
    peso = 3;
    linhas = ["a", "b"];
    talvez = "sim";
    ok = true;
  } else {
    rotulo = "pequeno";
    peso = 1;
    linhas = [];
    talvez = null;
    ok = false;
  }
  return `${rotulo}:${peso}:${linhas.length}:${talvez ?? "-"}:${ok}`;
}

console.log(primeiro([3, 4]), primeiro<string>([]), emPares(["a", "b", "c"]).length, ultimo(["x", "y"]));
console.log(classifica(20), classifica(2));
const r = new Relogio();
r.inicia(5, "tic");
r.para();
