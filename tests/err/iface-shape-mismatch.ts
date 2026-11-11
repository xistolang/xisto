// expect: help: parameter 'p' of Outro.executa is { empresa: string; codigo: number }, but Motor.executa passes { empresa: string; quantidade: number }
interface Motor {
  executa(p: { empresa: string; quantidade: number }, fator: number): string;
}
class Outro {
  executa(p: { empresa: string; codigo: number }, fator: number): string {
    return `${p.empresa} ${p.codigo * fator}`;
  }
}
function roda(m: Motor): void {
  console.log(m.executa({ empresa: "A", quantidade: 1 }, 1));
}
roda(new Outro());
