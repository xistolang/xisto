import { ApiProperty, ApiPropertyOptional } from "@nestjs/swagger";
import { Type } from "class-transformer";
import { ArrayMaxSize, IsArray, IsBoolean, IsDateString, IsIn, IsInt, IsNotEmpty, IsOptional, IsString, Matches, Max, MaxLength, Min, ValidateNested } from "class-validator";

export class TagDto {
  @IsString()
  @MaxLength(5)
  name: string;
}

export class CreateItemDto {
  @ApiProperty({ description: "the item's name" })
  @IsString()
  @IsNotEmpty()
  @MaxLength(20)
  name: string;

  @ApiPropertyOptional()
  @IsOptional()
  @IsInt()
  @Min(1)
  @Max(100)
  qty?: number;

  @IsIn(["A", "B"])
  kind: string;

  @IsOptional()
  @IsDateString()
  when?: string;

  @IsOptional()
  @Matches(/^[a-z]+$/)
  code?: string;

  @IsOptional()
  @IsArray()
  @ArrayMaxSize(2)
  @ValidateNested({ each: true })
  @Type(() => TagDto)
  tags?: TagDto[];

  @IsOptional()
  @IsBoolean()
  active?: boolean;
}

export class Item {
  id: number;
  name: string;
  qty: number;
  kind: string;
}

export class ItemList {
  items: Item[];
  total: number;
}

export class Info {
  app: string;
  limit: string;
  prefix: string;
  missing: boolean;
  alias: boolean;
}
