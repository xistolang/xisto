import { Conta as ContaLib, make } from './lib/alias-shadow.ts';
class Conta { w: string = "local"; }
let calls = 0;
class Box { n: int = 0; inc(k: int): int { this.n += k; return this.n; } }
const boxes: (Box | null)[] = [new Box()];
function get(): Box | null { calls++; return boxes[0]; }
console.log(get()!.inc(2), calls);
function greet(name: string, sep: string = ", "): string { return "hi" + sep + name; }
const sep = "!";
console.log(greet("a"), greet("b", sep), sep);
function outer(): string { const sep2 = "#"; return greet("c") + sep2; }
console.log(outer());
const a = new Conta(); const b: ContaLib = make();
console.log(a.w, b.v);
let x: Box;
x = new Box();
console.log(x.inc(3));
import { rotula, Fila } from './lib/alias-shadow.ts';
const PREFIXO = "local:";
console.log(rotula("a"), rotula("b", "x"), rotula("c"), PREFIXO);
const fila = new Fila();
let tops = 0;
function pega(): Fila | null { tops++; return fila.topo(); }
const f1 = pega()!;
console.log(f1.add(1), pega()!.add(2, 5), fila.add(3), tops, fila.itens.join(","));
