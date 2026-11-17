// a CreateDto of one feature folder; the other folder has a class of the same name with other fields
import { Check } from "../deco.ts";
import { Show } from "./show.ts";
export class CreateDto {
  @Check("string") code: string = "";
  @Check("int") company: number = 0;
}
@Show() export class UpdateDto extends PartialType(CreateDto) {}
