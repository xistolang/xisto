import { Check } from "./deco.ts";
enum Role { A = "a", B = "b" }
export class Base { @Check("int") id: number = 0; }
export class CreateDto extends Base {
  @Check("string") name: string = "";
  @Check("min") age: number = 0;
  role: Role = Role.A;
  tags?: string[];
}
