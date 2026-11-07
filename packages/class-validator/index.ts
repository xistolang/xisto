// class-validator: decorators record rules per class (xisto:reflect); validate() checks an instance.
import { ClassInfo, FieldInfo } from "xisto:reflect";

interface Rule {
  field: FieldInfo;
  kind: string;
}

const rules: Map<string, Rule[]> = new Map<string, Rule[]>();

function add(c: ClassInfo, f: FieldInfo, kind: string): void {
  if (!rules.has(c.id)) {
    rules.set(c.id, []);
  }
  const list = rules.get(c.id);
  if (list !== null) {
    list.push({ field: f, kind: kind });
  }
}

export function IsNotEmpty(): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    add(c, f, "isNotEmpty");
  };
}

export function IsEmail(): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    add(c, f, "isEmail");
  };
}

// local@domain.tld, the shape class-validator's isEmail accepts for ordinary addresses
export function isEmail(value: string): boolean {
  const parts: string[] = value.split("@");
  if (parts.length !== 2 || parts[0].length === 0 || value.includes(" ")) {
    return false;
  }
  const labels: string[] = parts[1].split(".");
  if (labels.length < 2) {
    return false;
  }
  for (const l of labels) {
    if (l.length === 0) {
      return false;
    }
  }
  return labels[labels.length - 1].length >= 2;
}

function empty(v: unknown): boolean {
  if (v === null) {
    return true;
  }
  if (typeof v === "string") {
    return (v as string) === "";
  }
  return false;
}

// class-validator's messages, in field order: "username should not be empty", "email must be an email"
export function validationMessages(target: unknown, classId: string): string[] {
  const out: string[] = [];
  const list = rules.get(classId);
  if (list === null) {
    return out;
  }
  for (const r of list) {
    const v = r.field.get(target);
    if (r.kind === "isNotEmpty" && empty(v)) {
      out.push(`${r.field.name} should not be empty`);
    }
    if (r.kind === "isEmail" && (empty(v) || !isEmail(v as string))) {
      out.push(`${r.field.name} must be an email`);
    }
  }
  return out;
}
