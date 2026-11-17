// class U extends PartialType(CreateDto) where two modules declare a CreateDto: the ClassInfo lists the fields of the
// one U's module imports (not the first one exported under that name)
import { ClassInfo } from "xisto:reflect";
import { UpdateDto } from "./lib/mapped/samename/billing.ts";
import { UpdateContractDto } from "./lib/mapped/samename/contract.ts";
const u = new UpdateContractDto();
u.code = "c1";
u.active = true;
console.log(JSON.stringify(u));
const ci: ClassInfo = UpdateContractDto;
console.log(ci.fields.map((f) => f.name + "=" + JSON.stringify(f.get(u))).join(" "));
const b = new UpdateDto();
b.company = 7;
console.log(JSON.stringify(b));
