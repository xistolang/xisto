// a numeric enum indexed by a string (its member's value, or undefined) and by a number (the member's name)
enum TipoProtocolo { MARCACAO = 1, CANCELAMENTO = 2, EXAME = 3 }
function num(tp: string): unknown[] {
  const n = TipoProtocolo[tp as keyof typeof TipoProtocolo] ?? tp;
  return [n, 9];
}
console.log(JSON.stringify(num('EXAME')), JSON.stringify(num('7')), TipoProtocolo[2]);

enum T { A = 1, B = 2, C = 3, D = 2 }
console.log(T[2], T[1]);
const i = 2;
console.log(T[i]);

const v: T = T.C;
console.log(T[v], T[T.A]);
