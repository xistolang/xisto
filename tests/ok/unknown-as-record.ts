// unknown as Record<string, unknown>: an object of a class is read as a Record of its fields (Object.entries walks
// any object's own properties in JavaScript), a Record is itself
class Contato {
  nome = "Ana";
  cpf = "12345678900";
  idade = 31;
  ativo: boolean | null = null;
}
function campos(data: unknown): string {
  const out: string[] = [];
  for (const [k, v] of Object.entries(data as Record<string, unknown>)) out.push(`${k}=${typeof v === "string" ? v : String(v)}`);
  return out.join(",");
}
const rec: Record<string, unknown> = { a: 1, b: "dois" };
console.log(campos(new Contato()));
console.log(campos(rec));
console.log(Object.keys(new Contato() as unknown as Record<string, unknown>).length);
