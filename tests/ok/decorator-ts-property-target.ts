// A property decorator written as TypeScript's, (target: object, key: string), that registers itself with
// target.constructor as class-validator's registerDecorator does: with and without its optional argument, two on
// one field, on a class whose ClassInfo also gets the field-decorator runner a mapped type uses (__xs_fdeco_).
// Expected: tsc --experimentalDecorators (its __decorate written out by hand), then Node
interface Registration {
  target: any;
  propertyName: string;
  tag: string;
}
const registrations: Registration[] = [];
function registerDecorator(r: Registration): void {
  registrations.push(r);
}
export function SingleSpaced(tag?: string) {
  return function (object: object, propertyName: string) {
    registerDecorator({ target: object.constructor, propertyName, tag: tag ?? "-" });
  };
}
function IsString() {
  return function (target: object, propertyName: string) {
    registerDecorator({ target: target.constructor, propertyName, tag: "string" });
  };
}
class LegacyDto {
  @IsString() @SingleSpaced() name: string = "";
  @SingleSpaced("title") title: string = "";
  plain: number = 0;
}
for (const r of registrations) console.log(r.target === LegacyDto, r.propertyName, r.tag);
console.log(new LegacyDto().plain);
