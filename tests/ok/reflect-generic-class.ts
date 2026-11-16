// XB-119: a generic decorated class (PagedResultDto<T>) has one ClassInfo, as JavaScript has one class: its fields typed
// where the type names no type parameter (unknown where it does), its field and class decorators applied
import { ClassInfo, FieldInfo } from "xisto:reflect";
function Prop(note: string): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => { console.log("prop", c.name, f.name, f.type.schema, note); };
}
function Model(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => { console.log("model", c.name, c.fields.map((f) => f.name + ":" + f.type.kind).join(" ")); };
}
class User { name: string = ""; }
@Model()
class PagedResultDto<T> {
  @Prop("the page") items: T[] = [];
  @Prop("count") total: number = 0;
  @Prop("maybe") next: string | null = null;
  cursor: Map<string, T> | null = null;
}
const p = new PagedResultDto<User>();
p.items.push(new User());
p.total = 1;
console.log(p.items.length, p.total, p.next);
