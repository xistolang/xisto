// a shape probe on an unknown value: (data as { toJSON?: unknown }).toJSON reads the field by name (undefined when
// the value has none), as TypeScript's unchecked cast lets the code ask
class Dto {
  cpf = "123";
}
class Stamp {
  at = 3;
  toJSON(): string {
    return `stamp-${this.at}`;
  }
}
function kind(data: unknown): string {
  if (data === null || typeof data !== "object") return "plain";
  if (typeof (data as { toJSON?: unknown }).toJSON === "function") return "has toJSON";
  const n = (data as { cpf?: string }).cpf;
  return n === undefined ? "no cpf" : `cpf ${n}`;
}
console.log(kind(new Dto()), kind(new Stamp()), kind(new Date(0)), kind(new Map<string, number>()), kind(5), kind(null));
