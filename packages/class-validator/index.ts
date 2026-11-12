// class-validator: decorators record rules per class (xisto:reflect). checkBody() does what Nest's ValidationPipe
// does with a JSON body: class-transformer's plainToInstance (the implicit conversion and the @Transform functions,
// see class-transformer's fieldValue), then class-validator's validate, with class-validator 0.14's messages in its
// order (the whitelist's first, then per property its rules in the order the decorators run: bottom up).
import { ClassInfo, FieldInfo, MethodInfo, TypeInfo } from "xisto:reflect";
import { fieldValue, typeOf } from "class-transformer";

interface Rule {
  field: FieldInfo;
  kind: string;
  n: number;
  m: number; // a second bound (Length's max; -1 none)
  list: string[];
  kinds: string; // per list entry: 'n' for a number value, anything else a string (IsIn compares by value and type)
  pattern: RegExp | null;
  custom: ClassInfo | null; // registerDecorator's validator: a @ValidatorConstraint class
  by: CustomValidator | null; // ValidateBy's validator
  cond: ((object: any, value: any) => boolean) | null; // ValidateIf's condition
  name: string; // ValidateBy's name
  constraints: unknown[];
  message: string; // the decorator's own message ("" for the default)
  messageFn: unknown; // or a function of the ValidationArguments that gives it (null: none)
}

const rules: Map<string, Rule[]> = new Map<string, Rule[]>();
const names: Map<string, string> = new Map<string, string>(); // class name -> class id
const infos: Map<string, ClassInfo> = new Map<string, ClassInfo>(); // class id -> its ClassInfo

function add(c: ClassInfo, f: FieldInfo, kind: string, n: number = 0, list: string[] = [], pattern: RegExp | null = null, kinds: string = "", o: ValidationOptions | null = null): Rule {
  names.set(c.name, c.id);
  infos.set(c.id, c);
  if (!rules.has(c.id)) {
    rules.set(c.id, []);
  }
  const r: Rule = { field: f, kind: kind, n: n, m: -1, list: list, kinds: kinds, pattern: pattern, custom: null, by: null, cond: null, name: "", constraints: [], message: "", messageFn: null };
  const given: unknown = o === null ? null : o.message;
  if (typeof given === "string") {
    r.message = given as string;
  } else if (typeof given === "function") {
    r.messageFn = given;
  }
  const all = rules.get(c.id);
  if (all !== null) {
    all.push(r);
  }
  return r;
}

function rule(kind: string, o: ValidationOptions, n: number = 0, list: string[] = [], pattern: RegExp | null = null): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    add(c, f, kind, n, list, pattern, "", o);
  };
}

/**
 * Options every decorator takes: `message` replaces the default one, as a string ($property, $value and $constraint1
 * are filled in) or a function of the ValidationArguments; `each` is accepted.
 */
export class ValidationOptions {
  message?: string | ((args: ValidationArguments) => string);
  each?: boolean;
}

/** What a custom validator's validate and defaultMessage receive. */
export interface ValidationArguments {
  value: any;
  constraints: any[];
  targetName: string;
  object: any;
  property: string;
}

/** ValidateBy's validator: validate(value, args) and, optionally, defaultMessage(args). */
export interface CustomValidator {
  validate: (value: any, validationArguments: ValidationArguments) => boolean;
  defaultMessage?: (validationArguments: ValidationArguments) => string;
}

/** What ValidateBy takes: the rule's name, its constraints and its validator. */
export class ValidateByOptions {
  name: string;
  constraints?: unknown[];
  validator: CustomValidator;
  async?: boolean;
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
  const r = add(o.target, f, "custom", 0, [], null, "", o.options ?? null);
  r.custom = o.validator;
}

/**
 * A rule from a validator object: validate(value, args) decides, defaultMessage(args) words the error (unless
 * the options give a message).
 * @example ValidateBy({ name: "even", validator: { validate: (v: unknown) => typeof v === "number" && v % 2 === 0 } })
 */
export function ValidateBy(options: ValidateByOptions, validationOptions: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    const r = add(c, f, "by", 0, [], null, "", validationOptions);
    r.by = options.validator;
    r.name = options.name;
    r.constraints = options.constraints ?? [];
  };
}

/** The property's other rules run only when condition(object, value) is true. */
export function ValidateIf(condition: (object: any, value: any) => boolean, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    const r = add(c, f, "validateIf", 0, [], null, "", o);
    r.cond = condition;
  };
}

/** No rule: the property is allowed by the whitelist (whitelist / forbidNonWhitelisted). */
export function Allow(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("allow", o);
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
function customMessage(r: Rule): string {
  const c = r.custom;
  if (c !== null) {
    const m = c.methods.find((x: MethodInfo): boolean => x.name === "defaultMessage");
    if (m !== null) {
      return m.invoke(c.construct([]), []) as string;
    }
  }
  return "$property is invalid";
}

/** Skips the field's other rules when it is null or absent. */
export function IsOptional(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isOptional", o);
}
/** "$property should not be empty" for "", null and absent. */
export function IsNotEmpty(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isNotEmpty", o);
}
export function IsDefined(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isDefined", o);
}
export function IsString(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isString", o);
}
/** Any number (class-validator's isNumber with no options). */
export function IsNumber(options: ValidationOptions = {}, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isNumber", o);
}
export function IsInt(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isInt", o);
}
/** A number greater than zero. */
export function IsPositive(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isPositive", o);
}
export function IsBoolean(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isBoolean", o);
}
export function IsArray(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isArray", o);
}
export function IsEmail(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isEmail", o);
}
/** An ISO 8601 date (2026-11-10 or 2026-11-10T12:30:00Z). */
export function IsDateString(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isDateString", o);
}
export function IsISO8601(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isIso8601", o);
}
export function MaxLength(max: int, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("maxLength", o, max);
}
export function MinLength(min: int, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("minLength", o, min);
}
/** A string whose length is between min and max (max left out: no upper bound). */
export function Length(min: int, max: int = -1, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    const r = add(c, f, "length", min, [], null, "", o);
    r.m = max;
  };
}
export function Min(min: number, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("min", o, min);
}
export function Max(max: number, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("max", o, max);
}
export function ArrayMaxSize(max: int, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("arrayMaxSize", o, max);
}
export function ArrayMinSize(min: int, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("arrayMinSize", o, min);
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
    add(c, f, "isIn", 0, texts, null, kinds, o);
  };
}
/** One of an enum's values: pass Object.values(MyEnum) (a string array), as the values to accept. */
export function IsEnum(values: string[], o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("isEnum", o, 0, values);
}
/** Matches the regular expression. */
export function Matches(pattern: RegExp, o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("matches", o, 0, [], pattern);
}
/** Validates the nested object (or each object of the array) with its own class's rules. */
export function ValidateNested(o: ValidationOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return rule("validateNested", o);
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

// a rule's default message ($property for the property's name), or "" when the value passes
function check(r: Rule, v: any): string {
  const k = r.kind;
  const isArr = Array.isArray(v);
  if (k === "isNotEmpty" && empty(v)) {
    return "$property should not be empty";
  } else if (k === "isDefined" && v === null) {
    return "$property should not be null or undefined";
  } else if (k === "isString" && typeof v !== "string") {
    return "$property must be a string";
  } else if (k === "isNumber" && (typeof v !== "number" || !isFinite(v as number))) {
    return "$property must be a number conforming to the specified constraints";
  } else if (k === "isInt" && (typeof v !== "number" || !Number.isInteger(v as number))) {
    return "$property must be an integer number";
  } else if (k === "isPositive" && (typeof v !== "number" || !((v as number) > 0))) {
    return "$property must be a positive number";
  } else if (k === "isBoolean" && typeof v !== "boolean") {
    return "$property must be a boolean value";
  } else if (k === "isArray" && !isArr) {
    return "$property must be an array";
  } else if (k === "isEmail" && (typeof v !== "string" || !isEmail(v as string))) {
    return "$property must be an email";
  } else if ((k === "isDateString" || k === "isIso8601") && (typeof v !== "string" || !isISO8601(v as string))) {
    return "$property must be a valid ISO 8601 date string";
  } else if (k === "maxLength" && (typeof v !== "string" || Number((v as string).length) > r.n)) {
    return `$property must be shorter than or equal to ${numberText(r.n)} characters`;
  } else if (k === "minLength" && (typeof v !== "string" || Number((v as string).length) < r.n)) {
    return `$property must be longer than or equal to ${numberText(r.n)} characters`;
  } else if (k === "length") {
    const ok = typeof v === "string" && Number((v as string).length) >= r.n && (r.m < 0 || Number((v as string).length) <= r.m);
    if (ok) {
      return "";
    }
    if (typeof v !== "string" || Number((v as string).length) < r.n) {
      return `$property must be longer than or equal to ${numberText(r.n)} characters`;
    }
    return `$property must be shorter than or equal to ${numberText(r.m)} characters`;
  } else if (k === "min" && (typeof v !== "number" || !((v as number) >= r.n))) { // (NaN fails, as min() does)
    return `$property must not be less than ${numberText(r.n)}`;
  } else if (k === "max" && (typeof v !== "number" || !((v as number) <= r.n))) {
    return `$property must not be greater than ${numberText(r.n)}`;
  } else if (k === "arrayMaxSize" && (!isArr || Number((v as unknown[]).length) > r.n)) {
    return `$property must contain no more than ${numberText(r.n)} elements`;
  } else if (k === "arrayMinSize" && (!isArr || Number((v as unknown[]).length) < r.n)) {
    return `$property must contain at least ${numberText(r.n)} elements`;
  } else if ((k === "isIn" || k === "isEnum") && !inList(v, r.list, r.kinds)) {
    return `$property must be one of the following values: ${r.list.join(", ")}`;
  } else if (k === "custom") {
    const c = r.custom;
    if (c !== null && !customValid(c, v)) {
      return customMessage(r);
    }
  } else if (k === "matches") {
    const p = r.pattern;
    if (p !== null && (typeof v !== "string" || !p.test(v as string))) {
      return `$property must match ${p.toString()} regular expression`;
    }
  }
  return "";
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

// $property, $value and $constraint1.. in a message, as class-validator fills them in
function words(message: string, r: Rule, name: string, v: any, className: string): string {
  let out = message;
  if (out.includes("$constraint1")) {
    const c1 = r.kind === "isIn" || r.kind === "isEnum" ? r.list.join(", ") : r.constraints.length > 0 ? String(r.constraints[0] as any) : numberText(r.n);
    out = out.split("$constraint1").join(c1);
  }
  if (out.includes("$value") && (typeof v === "string" || typeof v === "number" || typeof v === "boolean")) {
    out = out.split("$value").join(String(v)); // (class-validator fills in a primitive value only)
  }
  return out.split("$property").join(name).split("$target").join(className);
}

function args(r: Rule, className: string, name: string, v: any, obj: any): ValidationArguments {
  return { value: v, constraints: r.constraints, targetName: className, object: obj, property: name };
}

// one rule's message for a value that fails it, or ""
function failure(r: Rule, className: string, name: string, v: any, obj: any): string {
  if (r.kind === "by") {
    const by = r.by;
    if (by === null) {
      return "";
    }
    const a = args(r, className, name, v, obj);
    if (by.validate(v, a)) {
      return "";
    }
    if (r.messageFn !== null) {
      return words((r.messageFn as (a: ValidationArguments) => string)(a), r, name, v, className);
    }
    if (r.message.length > 0) {
      return words(r.message, r, name, v, className);
    }
    const dm = by.defaultMessage;
    return dm == null ? "" : words(dm(a), r, name, v, className);
  }
  const m = check(r, v);
  if (m.length === 0) {
    return "";
  }
  if (r.messageFn !== null) {
    return words((r.messageFn as (a: ValidationArguments) => string)(args(r, className, name, v, obj)), r, name, v, className);
  }
  return words(r.message.length > 0 ? r.message : m, r, name, v, className);
}

/** The options of Nest's ValidationPipe that change what checkBody does. */
export class BodyCheckOptions {
  whitelist?: boolean;
  forbidNonWhitelisted?: boolean;
  enableImplicitConversion?: boolean;
}

/** What checkBody gives: the messages of a 400 (none when the body passes) and the body as the instance holds it. */
export class BodyCheck {
  messages: string[] = [];
  json: string = "";
}

function fieldOf(classId: string, name: string): FieldInfo | null {
  const ci = infos.get(classId);
  if (ci === null) {
    return null;
  }
  return ci.fields.find((f: FieldInfo): boolean => f.name === name);
}

// plainToInstance: the plain object's keys, each with the value its field gets (nested objects of a class too)
function toInstance(plain: Record<string, unknown>, classId: string, implicit: boolean): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  const obj: any = plain;
  for (const k of Object.keys(plain)) {
    const f = fieldOf(classId, k);
    const raw: any = plain[k];
    if (f === null) {
      out[k] = raw;
      continue;
    }
    let v: any = fieldValue(classId, f, raw, obj, implicit);
    const nested = nestedClass(classId, f);
    if (nested.length > 0 && rules.has(nested) && v !== null && typeof v === "object") {
      if (Array.isArray(v)) {
        const items: unknown[] = [];
        for (const it of v as unknown[]) {
          const x: any = it;
          items.push(x !== null && typeof x === "object" && !Array.isArray(x) ? toInstance(x as Record<string, unknown>, nested, implicit) : x);
        }
        v = items;
      } else {
        v = toInstance(v as Record<string, unknown>, nested, implicit);
      }
    }
    out[k] = v;
  }
  return out;
}

function validateInto(target: Record<string, unknown>, classId: string, prefix: string, out: string[], o: BodyCheckOptions, instance: unknown = null): void {
  const list = rules.get(classId);
  if (list === null) {
    return;
  }
  const ci = infos.get(classId);
  const className = ci === null ? "" : ci.name;
  const obj: any = instance !== null ? instance : target; // (what ValidateIf and the validators get as the object)
  if (o.whitelist ?? false) {
    for (const k of Object.keys(target)) {
      if (!list.some((r: Rule): boolean => r.field.name === k)) {
        if (o.forbidNonWhitelisted ?? false) {
          out.push(`${prefix}property ${k} should not exist`);
        } else {
          delete target[k];
        }
      }
    }
  }
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
    const v: any = name in target ? target[name] : null; // (absent: undefined for class-validator)
    let skip = false;
    for (const r of mine) {
      if (r.kind === "isOptional" && v === null) {
        skip = true;
      }
      const cond = r.cond;
      if (r.kind === "validateIf" && cond !== null && !cond(obj, v)) {
        skip = true;
      }
    }
    if (skip) {
      continue;
    }
    const own: string[] = [];
    const nested: string[] = [];
    for (const r of mine) {
      if (r.kind === "validateNested") {
        const cls = nestedClass(classId, f);
        if (v === null || cls.length === 0 || typeof v !== "object") {
          continue;
        }
        if (Array.isArray(v)) {
          const items = v as unknown[];
          for (let i = 0; i < items.length; i++) {
            const it: any = items[i];
            if (it !== null && typeof it === "object" && !Array.isArray(it)) {
              validateInto(it as Record<string, unknown>, cls, `${prefix}${name}.${i}.`, nested, o);
            }
          }
        } else {
          validateInto(v as Record<string, unknown>, cls, `${prefix}${name}.`, nested, o);
        }
        continue;
      }
      if (r.kind === "isOptional" || r.kind === "validateIf" || r.kind === "allow") {
        continue;
      }
      const m = failure(r, className, name, v, obj);
      if (m.length > 0) {
        own.push(prefix + m);
      }
    }
    // Nest's ValidationPipe lists a property's nested errors in place of its own when it has both
    for (const m of nested.length > 0 ? nested : own) {
      out.push(m);
    }
  }
}

// the instance of the declared class: the data as it is, or without the values its fields cannot hold (a string
// where a number goes reads as absent: JavaScript would keep the string), or null
function decodeInstance(type: TypeInfo, inst: Record<string, unknown>, classId: string): unknown {
  try {
    return type.decode(JSON.stringify(inst));
  } catch (e) {
    const fit: Record<string, unknown> = {};
    for (const k of Object.keys(inst)) {
      const f = fieldOf(classId, k);
      const v: any = inst[k];
      if (f === null) {
        continue;
      }
      try {
        f.type.decode(JSON.stringify(v));
        fit[k] = v;
      } catch (fe) {
        continue;
      }
    }
    try {
      return type.decode(JSON.stringify(fit));
    } catch (e2) {
      return null;
    }
  }
}

/**
 * Nest's ValidationPipe on a JSON body: plainToInstance (implicit conversion, @Transform) and validate (whitelist,
 * rules). The messages are those of the 400; json is the instance's data, which the handler gets with transform.
 * A body that is not a JSON object is left to the decoder.
 */
export function checkBody(json: string, classId: string, o: BodyCheckOptions = {}, type: TypeInfo | null = null): BodyCheck {
  const res = new BodyCheck();
  res.json = json;
  let plain: any = null;
  try {
    plain = JSON.parse(json);
  } catch (e) {
    return res;
  }
  if (plain === null || typeof plain !== "object" || Array.isArray(plain)) {
    return res;
  }
  const inst = toInstance(plain as Record<string, unknown>, classId, o.enableImplicitConversion ?? false);
  res.json = JSON.stringify(inst);
  // the object ValidateIf's condition and a validator's arguments see: the class's instance when the data makes one
  let instance: unknown = null;
  if (type !== null) {
    instance = decodeInstance(type, inst, classId);
  }
  validateInto(inst, classId, "", res.messages, o, instance);
  return res;
}

/**
 * class-validator's messages for an object, as the ValidationPipe lists them ("username should not be empty").
 * `json` is the body the object was decoded from (keys it lacks count as undefined); without it, the object's own
 * JSON is validated.
 */
export function validationMessages(target: unknown, classId: string, json: string = ""): string[] {
  const text = json.length > 0 ? json : JSON.stringify(target);
  return checkBody(text, classId).messages;
}
