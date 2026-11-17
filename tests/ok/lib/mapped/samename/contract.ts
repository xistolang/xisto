import { CreateDto } from "./contract-create.ts";
import { Show } from "./show.ts";
@Show() export class UpdateContractDto extends PartialType(CreateDto) {}
