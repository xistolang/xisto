export class Conta { v: int = 1; }
export function make(): Conta { return new Conta(); }
const PREFIXO = "p:";
let contador = 0;
function proximo(): int { contador++; return contador; }
export function rotula(s: string, pre: string = PREFIXO, n: int = proximo()): string { return pre + s + String(n); }
export class Fila { itens: int[] = []; topo(): Fila | null { return this; } add(x: int, k: int = this.itens.length): int { this.itens.push(x + k); return this.itens.length; } }
