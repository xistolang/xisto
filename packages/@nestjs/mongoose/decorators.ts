// @Schema / @Prop for @nestjs/mongoose (a module apart: the decorator Schema and mongoose's Schema class never
// meet in one module)
import { ClassInfo, FieldInfo } from "xisto:reflect";
import { SchemaOptions, SchemaTypeOptions, SchemaTypes } from "mongoose";


/** @Prop()'s options: a path's (type, required, default, index, unique, enum, ref...) */
export type PropOptions = SchemaTypeOptions;

export interface PropDef {
  name: string;
  options: SchemaTypeOptions;
}

export const __propsByClass: Map<string, PropDef[]> = new Map<string, PropDef[]>();
export const __schemaOptionsByClass: Map<string, SchemaOptions> = new Map<string, SchemaOptions>();

// the schema type a field's TypeScript type gives (a @Prop without `type`)
function typeOfField(f: FieldInfo): unknown {
  const k = f.type.kind;
  if (k === "string" || k === "enum") {
    return SchemaTypes.String;
  }
  if (k === "number" || k === "int") {
    return SchemaTypes.Number;
  }
  if (k === "boolean") {
    return SchemaTypes.Boolean;
  }
  if (k === "array") {
    return SchemaTypes.Array;
  }
  if (f.type.id === "Date") {
    return SchemaTypes.Date;
  }
  return SchemaTypes.Mixed;
}

/** @Prop({ type: String, required: true }) on a field of a @Schema() class */
export function Prop(options: PropOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    const o: SchemaTypeOptions = options;
    if (o.type == null) {
      o.type = typeOfField(f);
    }
    const list = __propsByClass.get(c.id);
    if (list === null) {
      __propsByClass.set(c.id, [{ name: f.name, options: o }]);
    } else {
      list.push({ name: f.name, options: o });
    }
  };
}

/** @Schema({ collection: "users", timestamps: true }) on the class of a document */
export function Schema(options: SchemaOptions = {}): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __schemaOptionsByClass.set(c.id, options);
  };
}

/** @Virtual() (recorded with the class: there are no documents to compute it on without a driver) */
export function Virtual(options: Record<string, unknown> = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {};
}

