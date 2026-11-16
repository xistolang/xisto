import { ClassInfo, FieldInfo } from "xisto:reflect";
export function Check(kind: string): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => { console.log("check", c.name, f.name, kind, f.optional); };
}
