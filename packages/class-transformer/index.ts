// class-transformer for xisto: a body is built into its declared classes by the compiler (nested objects and arrays
// of classes included). @Type(() => Item) records the element class, which class-validator's @ValidateNested reads
// for arrays; @Transform(fn) records functions that toPlainInstance() runs on a JSON body the way plainToInstance
// does before Nest's ValidationPipe validates it.
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

/** Which way a transformation goes (plainToInstance is PLAIN_TO_CLASS). */
export enum TransformationType {
  PLAIN_TO_CLASS = 0,
  CLASS_TO_PLAIN = 1,
  CLASS_TO_CLASS = 2,
}

/** What a @Transform function receives: the value (after the implicit conversion), its key and the plain object. */
export interface TransformFnParams {
  value: any;
  key: string;
  obj: any;
  type: TransformationType;
}

/** Options of @Transform (accepted: the function runs when a body becomes an instance). */
export class TransformOptions {
  toClassOnly?: boolean;
  toPlainOnly?: boolean;
}

interface TransformEntry {
  field: string;
  fn: (params: TransformFnParams) => any;
}

const transforms: Map<string, TransformEntry[]> = new Map<string, TransformEntry[]>();

/**
 * A function that gives the field's value when a plain object (a request body) becomes an instance.
 * @example @Transform(({ value }) => (typeof value === "string" ? value.toLowerCase() : value))
 */
export function Transform(fn: (params: TransformFnParams) => any, options: TransformOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    if (options.toPlainOnly ?? false) {
      return;
    }
    const list = transforms.get(c.id);
    const e: TransformEntry = { field: f.name, fn: fn };
    if (list === null) {
      transforms.set(c.id, [e]);
    } else {
      list.push(e);
    }
  };
}

/** True when the class has @Transform functions. */
export function hasTransforms(classId: string): boolean {
  return transforms.has(classId);
}

/**
 * The value a field gets from a plain object's key, as plainToInstance gives it: the implicit conversion to the
 * field's declared type (enableImplicitConversion: String, Number or Boolean of a primitive), then the field's
 * @Transform functions in the order they were declared.
 */
export function fieldValue(classId: string, field: FieldInfo, value: any, obj: any, implicit: boolean): any {
  let v: any = value;
  if (implicit && v !== null && typeof v !== "object") {
    const k = field.type.kind;
    if (k === "string") {
      v = String(v);
    } else if (k === "number" || k === "int") {
      v = Number(v);
    } else if (k === "boolean") {
      v = v ? true : false;
    }
  }
  const list = transforms.get(classId);
  if (list !== null) {
    for (const t of list) {
      if (t.field === field.name) {
        v = t.fn({ value: v, key: field.name, obj: obj, type: TransformationType.PLAIN_TO_CLASS });
      }
    }
  }
  return v;
}
