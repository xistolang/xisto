import { Check } from "../deco.ts";
export class CreateDto {
  @Check("string") code: string = "";
  @Check("bool") active?: boolean;
}
