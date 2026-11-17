import { ClassInfo } from "xisto:reflect";
export function Show(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    console.log(c.name, c.fields.map((f) => f.name + (f.optional ? "?" : "") + ":" + f.type.schema).join(" "));
  };
}
