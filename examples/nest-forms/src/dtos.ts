import { Transform } from "class-transformer";
import { Allow, IsIn, IsInt, IsNotEmpty, IsNumber, IsOptional, IsString, Min, ValidateIf, ValidationArguments } from "class-validator";
import { BadRequestException, Injectable, PipeTransform } from "@nestjs/common";
import { Flag, RequiredInteger, RequiredText, SingleSpaced } from "./decorators";

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

  // a message made by a function of the ValidationArguments (its parameter typed by the option)
  @ValidateIf((_object, value) => value !== undefined)
  @IsIn([0, 1], {
    message: ({ value }) => (value === null ? "Kiosk is required." : `Kiosk: ${String(value)} is not valid, use 0 or 1.`),
  })
  kiosk?: number;

  // required only for a printing kiosk (the condition reads the DTO through a shape of its own)
  @ValidateIf((o: { printer?: number }) => o.printer === 1)
  @IsString()
  @IsNotEmpty()
  printerModel?: string;

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
  @SingleSpaced()
  name: string;

  /** @deprecated accepted and ignored */
  @Allow()
  userId?: unknown;
}

export class SearchQueryDto {
  @IsOptional()
  @Transform(({ value }) => (typeof value === "string" ? value.toLowerCase() : value))
  @IsIn(["asc", "desc"])
  sortOrder?: "asc" | "desc";

  @IsOptional()
  @IsInt()
  @Min(1)
  page?: number;

  @IsString()
  @IsNotEmpty()
  term: string;
}

// a route that takes no query: any key is refused, as the app's own pipe does
@Injectable()
export class NoQueryPipe implements PipeTransform {
  transform(value: unknown): unknown {
    const keys = value && typeof value === "object" ? Object.keys(value as Record<string, unknown>) : [];
    if (keys.length > 0) {
      throw new BadRequestException(keys.map((k: string) => `property ${k} should not exist`));
    }
    return null;
  }
}
