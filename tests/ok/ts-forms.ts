// TypeScript forms that code written for Node uses: `T | undefined` in types (xisto reads it as T | null),
// an import after declarations (ES hoists it), `void expr`, number and boolean literal types, `as const` on a
// typed literal.
function soma(x: number | undefined, y: number | null | undefined): number {
  return (x ?? 0) + (y ?? FATOR);
}
const FATOR: number = 10;

interface Passo { passe: 1 | 2 | 3; ativo: true | false; sinal?: -1 | 0 | 1 }
function peso(p: Passo): number { return p.passe * (p.ativo ? 2 : 1) + (p.sinal ?? 0); }

const NOMES: string[] = ["a", "b"] as const;

async function tarefa(rotulo: string): Promise<void> {
  console.log("tarefa", rotulo);
}
void tarefa("um");
const dispara = (): void => void tarefa("dois");
dispara();

let talvez: string | undefined = null;
console.log(soma(null, null), soma(1, 2), soma(5, undefinedFree()), talvez ?? "vazio");
console.log(peso({ passe: 3, ativo: true }), peso({ passe: 1, ativo: false, sinal: -1 }), NOMES.length, formata(7));

function undefinedFree(): number | undefined { return null; }

import { ORIGIN_LABEL } from "./lib/geometry.ts";
function formata(n: number): string { return ORIGIN_LABEL + String(n); }
