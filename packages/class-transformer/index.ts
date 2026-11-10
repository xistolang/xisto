// class-transformer for xisto: a body is already built into its declared classes by the compiler (nested objects
// and arrays of classes included), so @Type(() => Item) only records the element class, which class-validator's
// @ValidateNested reads for arrays.
import { ClassInfo, FieldInfo } from "xisto:reflect";

const types: Map<string, ClassInfo> = new Map<string, ClassInfo>();

/** The class of a nested field or of an array's elements: @Type(() => ItemDto). */
export function Type(fn: () => ClassInfo): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    types.set(c.id + "." + f.name, fn());
  };
}

/** The class @Type recorded for a field, or null. */
export function typeOf(classId: string, field: string): ClassInfo | null {
  return types.get(classId + "." + field);
}

/** Accepted: every field is exposed. */
export function Expose(): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {};
}
