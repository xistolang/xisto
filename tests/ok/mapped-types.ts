// class X extends PartialType(Y) (OmitType, PickType, IntersectionType): Y's fields copied in, optional for
// PartialType, with their initializers (as @nestjs/mapped-types); the names come from @nestjs/swagger in a program
class Base { id: number = 0; }
class CreateDto extends Base {
  name: string = "";
  age: number = 0;
  tags?: string[];
}
class Audit { by: string = "sys"; }
class UpdateDto extends PartialType(CreateDto) {}
class NoAge extends PartialType(OmitType(CreateDto, ["age"] as const)) {
  extra: boolean = true;
}
class OnlyName extends PickType(CreateDto, ["name"]) {}
class Both extends IntersectionType(OnlyName, Audit) {}
const u = new UpdateDto();
u.name = "x";
console.log(u.name, u.age ?? -1, u.id ?? -1, JSON.stringify(u));
const n = new NoAge();
console.log(JSON.stringify(n), n.extra, n.name ?? "none");
const parsed = JSON.parse('{"age":3}') as UpdateDto;
console.log(parsed.age, parsed.name ?? "none");
console.log(JSON.stringify(new OnlyName()), JSON.stringify(new Both()));
