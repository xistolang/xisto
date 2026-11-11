// A decorated field with a quoted name ('j_idt100:xmlSaida', a form field of a JSF page): its reflection reads it by name.
import { ClassInfo, FieldInfo } from "xisto:reflect";

const campos: FieldInfo[] = [];
function Campo(): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    campos.push(f);
  };
}
class Formulario {
  @Campo()
  "j_idt100:xmlSaida": string = "<enviBio/>";
  @Campo()
  "outro-campo"?: string;
  @Campo()
  simples: number = 3;
}
const f = new Formulario();
f["outro-campo"] = "x";
for (const c of campos) console.log(c.name, c.optional, JSON.stringify(c.get(f)));
