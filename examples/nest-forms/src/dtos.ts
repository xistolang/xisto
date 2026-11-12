import { Transform } from "class-transformer";
import { Allow, IsIn, IsNotEmpty, IsNumber, IsOptional, IsString, ValidateIf, ValidationArguments } from "class-validator";
import { Flag, RequiredInteger, RequiredText } from "./decorators";

export class CreateTabletDto {
  @RequiredText("Machine IP", "Machine IP is required.", 15)
  ip: string;

  @RequiredInteger("Company is required.", "Company is not valid.")
  company: number;
}

export class UpdateKioskDto {
  @ValidateIf((_object, value) => value !== undefined)
  @IsString()
  @IsNotEmpty()
  name?: string;

  @ValidateIf((_object, value) => value !== undefined)
  @Flag("Printer", "Printer is required.")
  printer?: number;

  @ValidateIf((_object, value) => value !== undefined)
  @IsIn(["S", "N"], { message: "Ticket: use S or N." })
  ticket?: string;

  @ValidateIf((_object, value) => value !== undefined)
  @IsNumber()
  active?: number;

  @ValidateIf((o: UpdateKioskDto) => o.ticket === "S")
  @IsString({ message: (args: ValidationArguments) => `${args.property} is required when ${args.targetName}.ticket is S` })
  note?: string;
}

export class SortDto {
  @IsOptional()
  @Transform(({ value }) => (typeof value === "string" ? value.toLowerCase() : value))
  @IsIn(["asc", "desc"])
  sortOrder?: "asc" | "desc";

  @IsString()
  name: string;
}

export class LegacyDto {
  @IsString()
  name: string;

  /** @deprecated accepted and ignored */
  @Allow()
  userId?: unknown;
}
