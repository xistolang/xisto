import { applyDecorators, SetMetadata, UseGuards } from "@nestjs/common";
import { Transform } from "class-transformer";
import { IsIn, ValidateBy, ValidationArguments } from "class-validator";
import { ScopeGuard } from "./scope.guard";

const isBlank = (value: unknown): boolean =>
  value === undefined || value === null || (typeof value === "string" && value.trim() === "");

// keeps the value as it came in the body (before the implicit conversion to the field's type)
const rawValue = () => Transform(({ obj, key }: { obj: Record<string, unknown>; key: string }) => obj[key]);

export function RequiredText(label: string, requiredMessage: string, maxLength: number): PropertyDecorator {
  return applyDecorators(
    rawValue(),
    ValidateBy({
      name: "requiredText",
      validator: {
        validate: (value: unknown) => typeof value === "string" && value.trim() !== "" && value.length <= maxLength,
        defaultMessage: (args) => {
          const value: unknown = args?.value;
          if (isBlank(value)) return requiredMessage;
          if (typeof value !== "string") return `${label} is not valid.`;
          return `${label} must have at most ${maxLength} characters.`;
        },
      },
    }),
  );
}

export function RequiredInteger(requiredMessage: string, invalidMessage: string): PropertyDecorator {
  return applyDecorators(
    rawValue(),
    ValidateBy({
      name: "requiredInteger",
      validator: {
        validate: (value: unknown) => typeof value === "number" && Number.isInteger(value),
        defaultMessage: (args) => (isBlank(args?.value) ? requiredMessage : invalidMessage),
      },
    }),
  );
}

export function Flag(label: string, requiredMessage: string): PropertyDecorator {
  return applyDecorators(
    Transform(({ obj, key }: { obj: Record<string, unknown>; key: string }) => obj[key]),
    IsIn([0, 1], {
      message: (args: ValidationArguments) => (isBlank(args.value) ? requiredMessage : `${label}: use 0 or 1 ($property, $value).`),
    }),
  );
}

export const RESOURCE_KEY = "resource";

export const Scoped = (resource: string = "local"): ClassDecorator =>
  applyDecorators(SetMetadata(RESOURCE_KEY, resource), UseGuards(ScopeGuard));
