// expect: the object the method is called on cannot be read twice
class Fila { itens: int[] = []; add(x: int, k: int = this.itens.length): int { this.itens.push(x + k); return this.itens.length; } }
const filas: Fila[] = [new Fila()];
function pega(): Fila { return filas[0]; }
console.log(pega().add(1));
