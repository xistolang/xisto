// class-validator: decorators record rules per class (xisto:reflect); validationMessages() checks an instance and
// gives class-validator 0.14's messages, in its order (per property, the property's rules in the order the
// decorators run: bottom up). The compiler has already built the body into the declared types, so a value of the
// wrong type never reaches these rules (the body decoder answers 400 first).
import { ClassInfo, FieldInfo, MethodInfo } from "xisto:reflect";
import { typeOf } from "class-transformer";

interface Rule {
  field: FieldInfo;
  kind: string;
  n: number;
  list: string[];
  kinds: string; // per list entry: 'n' for a number value, anything else a string (IsIn compares by value and type)
  pattern: RegExp | null;
  custom: ClassInfo | null; // registerDecorator's validator: a @ValidatorConstraint class
  message: string; // the decorator's own message ("" for the default)
}

const rules: Map<string, Rule[]> = new Map<string, Rule[]>();
const names: Map<string, string> = new Map<string, string>(); // class name -> class id

function add(c: ClassInfo, f: FieldInfo, kind: string, n: number = 0, list: string[] = [], pattern: RegExp | null = null, kinds: string = ""): void {
  names.set(c.name, c.id);
  if (!rules.has(c.id)) {
    rules.set(c.id, []);
  }
  const all = rules.get(c.id);
  if (all !== null) {
    all.push({ field: f, kind: kind, n: n, list: list, kinds: kinds, pattern: pattern, custom: null, message: "" });
  }
}

function rule(kind: string, n: number = 0, list: string[] = [], pattern: RegExp | null = null): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    add(c, f, kind, n, list, pattern);
  };
}

/** Options every decorator takes (`each` and `message` are accepted). */
export class ValidationOptions {
  message?: string;
  each?: boolean;
}

/** What @ValidatorConstraint({ name, async }) takes. */
export class ValidatorConstraintOptions {
  name?: string;
  async?: boolean;
}
/** Marks a class whose validate(value) and defaultMessage() a custom decorator uses (registerDecorator). */
export function ValidatorConstraint(o: ValidatorConstraintOptions = {}): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {};
}
/** What a custom constraint class implements: validate(value): boolean and, optionally, defaultMessage(): string. */
export interface ValidatorConstraintInterface {}
/** What registerDecorator takes: the class and property, and the constraint class that validates it. */
export class ValidationDecoratorOptions {
  target: ClassInfo;
  propertyName: string;
  name?: string;
  options?: ValidationOptions | null;
  constraints?: unknown[];
  validator: ClassInfo;
}
/** A custom rule: the constraint's validate(value) runs on the property (a missing value fails without a call). */
export function registerDecorator(o: ValidationDecoratorOptions): void {
  const f = o.target.fields.find((x: FieldInfo): boolean => x.name === o.propertyName);
  if (f === null) {
    return;
  }
  add(o.target, f, "custom");
  const all = rules.get(o.target.id);
  if (all !== null) {
    const r = all[all.length - 1];
    r.custom = o.validator;
    const opts = o.options;
    r.message = opts !== null && opts.message !== null ? opts.message : "";
  }
}

// a custom rule's verdict: the constraint class's validate(value)
function customValid(c: ClassInfo, v: unknown): boolean {
  if (v === null) {
    return false;
  }
  const m = c.methods.find((x: MethodInfo): boolean => x.name === "validate");
  if (m === null) {
    return true;
  }
  return (m.invoke(c.construct([]), [v]) as boolean) === true;
}
function customMessage(r: Rule, prop: string): string {
  if (r.message.length > 0) {
    return r.message;
  }
  const c = r.custom;
  if (c !== null) {
    const m = c.methods.find((x: MethodInfo): boolean => x.name === "defaultMessage");
    if (m !== null) {
      return m.invoke(c.construct([]), []) as string;
    }
  }
  return `${prop} is invalid`;
}

/** Skips the field's other rules when it is null or absent. */
export function IsOptional(): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isOptional");
}
/** "$property should not be empty" for "", null and absent. */
export function IsNotEmpty(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isNotEmpty");
}
export function IsDefined(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isDefined");
}
export function IsString(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isString");
}
/** Any number (class-validator's isNumber with no options). */
export function IsNumber(options: ValidationOptions = {}, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isNumber");
}
export function IsInt(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isInt");
}
export function IsBoolean(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isBoolean");
}
export function IsArray(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isArray");
}
export function IsEmail(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isEmail");
}
/** An ISO 8601 date (2026-11-10 or 2026-11-10T12:30:00Z). */
export function IsDateString(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isDateString");
}
export function IsISO8601(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isIso8601");
}
export function MaxLength(max: int, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("maxLength", max);
}
export function MinLength(min: int, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("minLength", min);
}
export function Min(min: number, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("min", min);
}
export function Max(max: number, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("max", max);
}
export function ArrayMaxSize(max: int, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("arrayMaxSize", max);
}
export function ArrayMinSize(min: int, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("arrayMinSize", min);
}
/** One of the given values. */
export function IsIn(values: (string | number)[], o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  const texts: string[] = [];
  let kinds = "";
  for (const v of values) {
    texts.push(String(v));
    kinds += typeof v === "number" ? "n" : "s";
  }
  return (c: ClassInfo, f: FieldInfo): void => {
    add(c, f, "isIn", 0, texts, null, kinds);
  };
}
/** One of an enum's values: pass Object.values(MyEnum) (a string array), as the values to accept. */
export function IsEnum(values: string[], o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isEnum", 0, values);
}
/** Matches the regular expression. */
export function Matches(pattern: RegExp, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("matches", 0, [], pattern);
}
/** Validates the nested object (or each object of the array) with its own class's rules. */
export function ValidateNested(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("validateNested");
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

const ISO_DATE = /^\d{4}-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])(T([01]\d|2[0-3]):[0-5]\d(:[0-5]\d(\.\d+)?)?(Z|[+-]([01]\d|2[0-3]):?[0-5]\d)?)?$/;

/** class-validator's isISO8601 for the common forms: a date, or a date and time with an optional offset. */
export function isISO8601(value: string): boolean {
  return ISO_DATE.test(value);
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

function numberText(n: number): string {
  return `${n}`;
}

function check(r: Rule, v: unknown, prop: string, out: string[], isArr: boolean): void {
  const k = r.kind;
  if (k === "isNotEmpty" && empty(v)) {
    out.push(`${prop} should not be empty`);
  } else if (k === "isDefined" && v === null) {
    out.push(`${prop} should not be null or undefined`);
  } else if (k === "isString" && typeof v !== "string") {
    out.push(`${prop} must be a string`);
  } else if (k === "isNumber" && (typeof v !== "number" || !isFinite(v as number))) {
    out.push(`${prop} must be a number conforming to the specified constraints`);
  } else if (k === "isInt" && (typeof v !== "number" || !Number.isInteger(v as number))) {
    out.push(`${prop} must be an integer number`);
  } else if (k === "isBoolean" && typeof v !== "boolean") {
    out.push(`${prop} must be a boolean value`);
  } else if (k === "isArray" && !isArr) {
    out.push(`${prop} must be an array`);
  } else if (k === "isEmail" && (typeof v !== "string" || !isEmail(v as string))) {
    out.push(`${prop} must be an email`);
  } else if ((k === "isDateString" || k === "isIso8601") && (typeof v !== "string" || !isISO8601(v as string))) {
    out.push(`${prop} must be a valid ISO 8601 date string`);
  } else if (k === "maxLength" && (typeof v !== "string" || Number((v as string).length) > r.n)) {
    out.push(`${prop} must be shorter than or equal to ${numberText(r.n)} characters`);
  } else if (k === "minLength" && (typeof v !== "string" || Number((v as string).length) < r.n)) {
    out.push(`${prop} must be longer than or equal to ${numberText(r.n)} characters`);
  } else if (k === "min" && (typeof v !== "number" || (v as number) < r.n)) {
    out.push(`${prop} must not be less than ${numberText(r.n)}`);
  } else if (k === "max" && (typeof v !== "number" || (v as number) > r.n)) {
    out.push(`${prop} must not be greater than ${numberText(r.n)}`);
  } else if (k === "arrayMaxSize" && (!isArr || Number((v as unknown[]).length) > r.n)) {
    out.push(`${prop} must contain no more than ${numberText(r.n)} elements`);
  } else if (k === "arrayMinSize" && (!isArr || Number((v as unknown[]).length) < r.n)) {
    out.push(`${prop} must contain at least ${numberText(r.n)} elements`);
  } else if ((k === "isIn" || k === "isEnum") && !inList(v, r.list, r.kinds)) {
    out.push(`${prop} must be one of the following values: ${r.list.join(", ")}`);
  } else if (k === "custom") {
    const c = r.custom;
    if (c !== null && !customValid(c, v)) {
      out.push(customMessage(r, prop));
    }
  } else if (k === "matches") {
    const p = r.pattern;
    if (p !== null && (typeof v !== "string" || !p.test(v as string))) {
      out.push(`${prop} must match ${p.toString()} regular expression`);
    }
  }
}

// class-validator's isIn: some(p => p === value), so the string "1" is not the number 1
function inList(v: unknown, list: string[], kinds: string): boolean {
  if (typeof v === "string") {
    for (let i = 0; i < list.length; i++) {
      if (kinds.charAt(i) !== "n" && list[i] === (v as string)) {
        return true;
      }
    }
    return false;
  }
  if (typeof v === "number") {
    const t = `${v as number}`;
    for (let i = 0; i < list.length; i++) {
      if (kinds.charAt(i) === "n" && list[i] === t) {
        return true;
      }
    }
  }
  return false;
}

// the class id of a nested field: @Type(() => X) first, then the field's declared class
function nestedClass(owner: string, f: FieldInfo): string {
  const t = typeOf(owner, f.name);
  if (t !== null) {
    return t.id;
  }
  if (f.type.kind === "class") {
    return f.type.id;
  }
  let n = f.type.id;
  if (n.endsWith("[]")) {
    n = n.slice(0, n.length - 2);
  }
  const hash = n.lastIndexOf("#");
  if (hash >= 0) {
    n = n.slice(hash + 1, n.length);
  }
  return names.get(n) || "";
}

// The JSON the body arrived as, read just enough to tell a key that is absent (undefined for class-validator) from
// one that is there: the decoded object holds "" or 0 for both.
class Skim {
  text: string;
  at: int = 0;
  constructor(text: string) {
    this.text = text;
  }
  space(): void {
    while (this.at < this.text.length && " \t\r\n".includes(this.text.slice(this.at, this.at + 1))) {
      this.at++;
    }
  }
  str(): string {
    let out = "";
    this.at++; // the opening quote
    while (this.at < this.text.length && this.text.slice(this.at, this.at + 1) !== "\"") {
      if (this.text.slice(this.at, this.at + 1) === "\\") {
        this.at++;
      }
      out = out + this.text.slice(this.at, this.at + 1);
      this.at++;
    }
    this.at++;
    return out;
  }
  // skips one value and gives its text
  value(): string {
    this.space();
    const start = this.at;
    let depth = 0;
    while (this.at < this.text.length) {
      const ch = this.text.slice(this.at, this.at + 1);
      if (ch === "\"") {
        this.str();
        if (depth === 0) {
          break;
        }
        continue;
      }
      if (ch === "{" || ch === "[") {
        depth++;
      } else if (ch === "}" || ch === "]") {
        if (depth === 0) {
          break;
        }
        depth--;
        if (depth === 0) {
          this.at++;
          break;
        }
      } else if (depth === 0 && (ch === "," || ch === " " || ch === "\n" || ch === "\r" || ch === "\t")) {
        break;
      }
      this.at++;
    }
    return this.text.slice(start, this.at);
  }
}

// the keys of a JSON object and their values' text (an empty map when the text is not an object)
function objectKeys(json: string): Map<string, string> {
  const out = new Map<string, string>();
  const s = new Skim(json);
  s.space();
  if (s.at >= json.length || json.slice(s.at, s.at + 1) !== "{") {
    return out;
  }
  s.at++;
  while (s.at < json.length) {
    s.space();
    if (s.at >= json.length || json.slice(s.at, s.at + 1) !== "\"") {
      break;
    }
    const key = s.str();
    s.space();
    s.at++; // the colon
    out.set(key, s.value());
    s.space();
    if (s.at < json.length && json.slice(s.at, s.at + 1) === ",") {
      s.at++;
    }
  }
  return out;
}

// the elements of a JSON array, as text
function arrayItems(json: string): string[] {
  const out: string[] = [];
  const s = new Skim(json);
  s.space();
  if (s.at >= json.length || json.slice(s.at, s.at + 1) !== "[") {
    return out;
  }
  s.at++;
  while (s.at < json.length) {
    s.space();
    if (s.at >= json.length || json.slice(s.at, s.at + 1) === "]") {
      break;
    }
    out.push(s.value());
    s.space();
    if (s.at < json.length && json.slice(s.at, s.at + 1) === ",") {
      s.at++;
    }
  }
  return out;
}

function validateInto(target: unknown, classId: string, prefix: string, out: string[], json: string): void {
  const list = rules.get(classId);
  if (list === null) {
    return;
  }
  const keys = objectKeys(json);
  const knowsKeys = json.length > 0;
  // the properties in the order their first rule was recorded; per property, its rules in recording order
  const order: string[] = [];
  for (const r of list) {
    if (!order.includes(r.field.name)) {
      order.push(r.field.name);
    }
  }
  for (const name of order) {
    const mine = list.filter((r: Rule): boolean => r.field.name === name);
    const f = mine[0].field;
    let v = f.get(target);
    const raw = keys.get(name) || "";
    if (knowsKeys && (!keys.has(name) || raw === "null")) {
      v = null; // absent from the body: undefined for class-validator
    }
    const optional = mine.some((r: Rule): boolean => r.kind === "isOptional");
    if (optional && v === null) {
      continue;
    }
    const prop = prefix + name;
    // (an array by the field's declared type: the value is held as unknown here)
    const isArr = v !== null && (f.type.kind === "array" || f.type.id.endsWith("[]"));
    const own: string[] = [];
    const nested: string[] = [];
    for (const r of mine) {
      if (r.kind === "validateNested") {
        const cls = nestedClass(classId, f);
        if (v === null || cls.length === 0) {
          continue;
        }
        if (isArr) {
          const items = v as unknown[];
          const texts = arrayItems(raw);
          for (let i = 0; i < items.length; i++) {
            validateInto(items[i], cls, `${prop}.${i}.`, nested, i < texts.length ? texts[i] : "");
          }
        } else {
          validateInto(v, cls, `${prop}.`, nested, raw);
        }
        continue;
      }
      check(r, v, prop, own, isArr);
    }
    // Nest's ValidationPipe lists a property's nested errors in place of its own when it has both
    for (const m of nested.length > 0 ? nested : own) {
      out.push(m);
    }
  }
}

// class-validator's messages, as the ValidationPipe lists them: "username should not be empty", "email must be an email"
// `json` is the body the object was decoded from, so keys it lacks count as undefined ("" when unknown).
export function validationMessages(target: unknown, classId: string, json: string = ""): string[] {
  const out: string[] = [];
  validateInto(target, classId, "", out, json);
  return out;
}
