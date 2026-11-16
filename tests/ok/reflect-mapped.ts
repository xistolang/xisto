// XB-120: class X extends PartialType(Y) (OmitType, PickType): the ClassInfo lists the fields copied in (optional under
// PartialType, typed as the copies), and Y's field decorators run again on them, as @nestjs/mapped-types inherits them
import { ClassInfo } from "xisto:reflect";
import { CreateDto } from "./lib/mapped/create.ts";
import { Check } from "./lib/mapped/deco.ts";
function Show(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    console.log(c.name, c.fields.map((f) => f.name + (f.optional ? "?" : "") + ":" + f.type.schema).join(" "));
  };
}
@Show() class UpdateDto extends PartialType(CreateDto) {}
@Show() class NoAge extends OmitType(CreateDto, ["age"] as const) { @Check("bool") extra: boolean = true; }
@Show() class OnlyName extends PickType(CreateDto, ["name"]) {}
const u = new UpdateDto();
u.name = "x";
console.log(JSON.stringify(u));
const ci: ClassInfo = UpdateDto;
console.log(ci.fields.map((f) => f.name + "=" + JSON.stringify(f.get(u))).join(" "));
