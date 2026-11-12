// mongoose (v8) for xisto, over the mongodb package (OP_MSG, BSON, pool, SCRAM-SHA-256: packages/mongodb).
//
// ObjectId is bson's (from mongodb, re-exported). Schema: paths, options, indexes, clone, set/get, virtuals.
// Connection (createConnection, MongooseModule's): openUri/asPromise select the server through the driver
// (serverSelectionTimeoutMS; a failure is MongooseServerSelectionError with the driver's last error, and the
// connection emits 'disconnected' then 'error'), readyState 0 -> 2 -> 1 and 3 -> 0 on close, 'connected'/'open'/
// 'disconnected'/'close' events, db.command, useDb (the same client, another database), getClient().options.
// A model made on an open connection is initialised as mongoose does: createCollection (autoCreate) and its indexes
// (autoIndex: the schema's option, else the connection's, else true).
// Model: find/findOne/findById (sort, skip, limit, select, lean), countDocuments, estimatedDocumentCount, exists,
// distinct, create/insertMany (casting by the schema: defaults, lowercase/trim, ObjectId/Date/Number paths, _id,
// timestamps, __v, strict), updateOne/updateMany (timestamps, $setOnInsert defaults on upsert), findOneAndUpdate/
// findByIdAndUpdate/findOneAndDelete/findByIdAndDelete (findAndModify), deleteOne/deleteMany, aggregate, bulkWrite.
// Filters are cast by the schema (a string where the path is an ObjectId or _id becomes an ObjectId, a string or
// number where it is a Date becomes a Date). Results are decoded into what the query gives (T, T[], counts, the
// result classes): documents are objects of T (or the lean type), plain data with the fields T declares.
// Node: a document is T itself (no save(), toObject() or extra fields T does not declare: a field T declares as
// required that the stored document lacks fails the decode); `new Types.ObjectId(x)` is written `new ObjectId(x)`
// (Types.ObjectId as a type and Types.ObjectId.isValid/createFromHexString work); a default that is a bare function
// value such as Date.now gives null (write () => Date.now()); a RegExp value in a filter is not supported (use
// { $regex, $options }); populate is ignored; middleware (pre/post hooks) and validation are not run.
import { ClassInfo } from "xisto:reflect";
import { Db, MongoBulkWriteError, MongoClient, MongoClientOptions, MongoServerError, MongoServerSelectionError, ObjectId, validObjectId, writeReply } from "mongodb";
export { BSONError, MongoServerError, ObjectId } from "mongodb";

/** mongoose.isValidObjectId(v): an ObjectId, 24 hex digits, or a number */
export function isValidObjectId(v: unknown): boolean {
  return validObjectId(v);
}

/** mongoose.isObjectIdOrHexString(v): an ObjectId or 24 hex digits */
export function isObjectIdOrHexString(v: unknown): boolean {
  if (v instanceof ObjectId) {
    return true;
  }
  return typeof v === "string" && /^[0-9a-fA-F]{24}$/.test(v as string);
}

// ---------- schema types ----------

/** A schema type as a value (SchemaTypes.Mixed, Types.ObjectId in `type:`): its name */
export class SchemaTypeRef {
  readonly name: string;
  constructor(name: string) {
    this.name = name;
  }
}

/** Types.ObjectId as a value: the ObjectId schema type, with bson's static helpers */
export class ObjectIdStatic extends SchemaTypeRef {
  constructor() {
    super("ObjectId");
  }
  isValid(id: unknown): boolean {
    return validObjectId(id);
  }
  createFromHexString(hex: string): ObjectId {
    return new ObjectId(hex);
  }
  createFromTime(seconds: number): ObjectId {
    return ObjectId.createFromTime(seconds);
  }
}

const OBJECT_ID_TYPE = new ObjectIdStatic();

/** mongoose.Types: Types.ObjectId (as a type: the ObjectId class) */
export class Types {
  static readonly ObjectId: ObjectIdStatic = OBJECT_ID_TYPE;
}

/** mongoose.SchemaTypes (Schema.Types): the schema types by name, for `type:` */
export class SchemaTypes {
  static readonly String: SchemaTypeRef = new SchemaTypeRef("String");
  static readonly Number: SchemaTypeRef = new SchemaTypeRef("Number");
  static readonly Boolean: SchemaTypeRef = new SchemaTypeRef("Boolean");
  static readonly Date: SchemaTypeRef = new SchemaTypeRef("Date");
  static readonly ObjectId: SchemaTypeRef = new SchemaTypeRef("ObjectId");
  static readonly Mixed: SchemaTypeRef = new SchemaTypeRef("Mixed");
  static readonly Array: SchemaTypeRef = new SchemaTypeRef("Array");
  static readonly Buffer: SchemaTypeRef = new SchemaTypeRef("Buffer");
  static readonly Decimal128: SchemaTypeRef = new SchemaTypeRef("Decimal128");
  static readonly Map: SchemaTypeRef = new SchemaTypeRef("Map");
  static readonly UUID: SchemaTypeRef = new SchemaTypeRef("UUID");
  static readonly BigInt: SchemaTypeRef = new SchemaTypeRef("BigInt");
  static readonly Double: SchemaTypeRef = new SchemaTypeRef("Double");
  static readonly Int32: SchemaTypeRef = new SchemaTypeRef("Int32");
}

/** a path's options: `@Prop({ ... })` or a field of `new Schema({ ... })` */
export interface SchemaTypeOptions {
  /** String, Number, Boolean, Date, [String], Types.ObjectId, SchemaTypes.Mixed, a Schema (a subdocument) */
  type?: unknown;
  required?: boolean;
  /** a value, or a function that makes it (() => new Date(), Date.now) */
  default?: unknown;
  index?: boolean;
  unique?: boolean;
  sparse?: boolean;
  enum?: string[];
  ref?: string;
  lowercase?: boolean;
  uppercase?: boolean;
  trim?: boolean;
  min?: number;
  max?: number;
  minlength?: number;
  maxlength?: number;
  select?: boolean;
  immutable?: boolean;
  expires?: number;
  alias?: string;
  _id?: boolean;
}

/** A path of a schema (schema.path("x")): its type's name as mongoose calls it ("String", "Array", "Embedded"...) */
export class SchemaType {
  path: string = "";
  instance: string = "Mixed";
  isRequired: boolean = false;
  options: SchemaTypeOptions = {};
  /** enum's values (String paths) */
  enumValues: string[] = [];
  /** the type of an array's elements */
  caster: SchemaType | null = null;
  /** a subdocument's schema (Embedded paths) */
  schema: Schema | null = null;

  get defaultValue(): unknown {
    return this.options.default ?? null;
  }
}

/** new Schema(definition, options) / @Schema(options) */
export interface SchemaOptions {
  collection?: string;
  timestamps?: boolean;
  _id?: boolean;
  id?: boolean;
  versionKey?: unknown;
  autoIndex?: boolean;
  autoCreate?: boolean;
  strict?: boolean;
  minimize?: boolean;
  bufferCommands?: boolean;
  capped?: number;
  discriminatorKey?: string;
}

/** a virtual path (recorded: there are no documents to compute it on without a driver) */
export class VirtualType {
  path: string;
  getters: int = 0;
  setters: int = 0;
  constructor(path: string) {
    this.path = path;
  }
  get(fn: (value: unknown) => unknown): VirtualType {
    this.getters++;
    return this;
  }
  set(fn: (value: unknown) => void): VirtualType {
    this.setters++;
    return this;
  }
}

class SchemaIndex {
  fields: Record<string, unknown> = {};
  options: Record<string, unknown> = {};
}

// the name mongoose gives a `type:` value
function typeName(t: unknown): string {
  if (t === null) {
    return "Mixed";
  }
  if (Array.isArray(t)) {
    return "Array";
  }
  if (t instanceof Schema) {
    return "Embedded";
  }
  if (t instanceof SchemaTypeRef) {
    return (t as SchemaTypeRef).name;
  }
  if (typeof t === "string") {
    const s = t as string;
    return s === "ObjectID" ? "ObjectId" : s;
  }
  const ci = t as ClassInfo;
  if (ci.id === "String" || ci.id === "Number" || ci.id === "Boolean" || ci.id === "Date") {
    return ci.id;
  }
  return "Mixed";
}

function makePath(name: string, o: SchemaTypeOptions): SchemaType {
  const st = new SchemaType();
  st.path = name;
  st.options = o;
  st.instance = typeName(o.type ?? null);
  st.isRequired = o.required === true;
  if (o.enum !== null && o.enum !== undefined) {
    st.enumValues = o.enum;
  }
  if (st.instance === "Array") {
    const elems = o.type as unknown[];
    const c = new SchemaType();
    c.path = name + ".$";
    c.instance = elems.length > 0 ? typeName(elems[0]) : "Mixed";
    if (elems.length > 0 && elems[0] instanceof Schema) {
      c.schema = elems[0] as Schema;
    }
    st.caster = c;
  } else if (st.instance === "Embedded") {
    st.schema = o.type as Schema;
  }
  return st;
}

export class Schema<T = unknown> {
  /** the paths by name, in order (_id after the fields, then createdAt/updatedAt with timestamps) */
  paths: Record<string, SchemaType> = {};
  /** virtual paths */
  virtuals: Record<string, VirtualType> = {};
  opts: Record<string, unknown> = {};
  private defs: Record<string, SchemaTypeOptions> = {};
  private own: SchemaIndex[] = [];
  private plugins: int = 0;

  constructor(definition: Record<string, SchemaTypeOptions> | null = null, options: SchemaOptions | null = null) {
    this.opts["strict"] = true;
    this.opts["versionKey"] = "__v";
    this.opts["_id"] = true;
    this.opts["autoIndex"] = null;
    if (options !== null) {
      if (options.collection != null) this.opts["collection"] = options.collection;
      if (options.timestamps != null) this.opts["timestamps"] = options.timestamps;
      if (options._id != null) this.opts["_id"] = options._id;
      if (options.id != null) this.opts["id"] = options.id;
      if (options.versionKey != null) this.opts["versionKey"] = options.versionKey;
      if (options.autoIndex != null) this.opts["autoIndex"] = options.autoIndex;
      if (options.autoCreate != null) this.opts["autoCreate"] = options.autoCreate;
      if (options.strict != null) this.opts["strict"] = options.strict;
      if (options.minimize != null) this.opts["minimize"] = options.minimize;
      if (options.bufferCommands != null) this.opts["bufferCommands"] = options.bufferCommands;
      if (options.capped != null) this.opts["capped"] = options.capped;
      if (options.discriminatorKey != null) this.opts["discriminatorKey"] = options.discriminatorKey;
    }
    if (definition !== null) {
      for (const [k, v] of Object.entries(definition)) {
        this.defs[k] = v;
        this.paths[k] = makePath(k, v);
      }
    }
    if (this.opts["_id"] === true) {
      this.paths["_id"] = makePath("_id", { type: SchemaTypes.ObjectId });
    }
    if (this.opts["timestamps"] === true) {
      this.paths["createdAt"] = makePath("createdAt", { type: SchemaTypes.Date });
      this.paths["updatedAt"] = makePath("updatedAt", { type: SchemaTypes.Date });
    }
  }

  /** schema.add({ field: { type: String } }) */
  add(definition: Record<string, SchemaTypeOptions>): Schema<T> {
    for (const [k, v] of Object.entries(definition)) {
      this.defs[k] = v;
      this.paths[k] = makePath(k, v);
    }
    return this;
  }

  /** the path's type, or null */
  path(name: string): SchemaType | null {
    if (name.endsWith(".$")) { /* the type of an array path's elements */
      const arr = this.paths[name.substring(0, name.length - 2)] ?? null;
      if (arr !== null && arr.caster !== null) {
        return arr.caster;
      }
    }
    const have = this.paths[name] ?? null;
    if (have !== null) {
      return have;
    }
    const dot = name.indexOf(".");
    if (dot > 0) { /* under a Mixed path, every subpath is Mixed */
      const top = this.paths[name.substring(0, dot)] ?? null;
      if (top !== null && top.instance === "Mixed") {
        return makePath(name, { type: SchemaTypes.Mixed });
      }
    }
    return null;
  }

  /** each path, in order */
  eachPath(fn: (name: string, type: SchemaType) => void): Schema<T> {
    for (const [k, v] of Object.entries(this.paths)) {
      fn(k, v);
    }
    return this;
  }

  /** schema.index({ a: 1, b: -1 }, { unique: true }) */
  index(fields: Record<string, unknown>, options: Record<string, unknown> = {}): Schema<T> {
    const ix = new SchemaIndex();
    ix.fields = fields;
    ix.options = options;
    this.own.push(ix);
    return this;
  }

  /** [fields, options] of each index: the paths' own (index/unique/sparse) first, then schema.index()'s; mongoose
   *  adds background: true to the options */
  indexes(): unknown[][] {
    const out: unknown[][] = [];
    for (const [k, p] of Object.entries(this.paths)) {
      const o = p.options;
      if (o.index === true || o.unique === true || o.sparse === true || o.expires != null) {
        const f: Record<string, unknown> = {};
        f[k] = 1;
        const opt: Record<string, unknown> = {};
        if (o.unique === true) opt["unique"] = true;
        if (o.sparse === true) opt["sparse"] = true;
        if (o.expires != null) opt["expireAfterSeconds"] = o.expires;
        opt["background"] = true;
        out.push([f, opt]);
      }
    }
    for (const ix of this.own) {
      const opt: Record<string, unknown> = {};
      for (const [k, v] of Object.entries(ix.options)) {
        opt[k] = v;
      }
      if (!("background" in opt)) {
        opt["background"] = true;
      }
      out.push([ix.fields, opt]);
    }
    return out;
  }

  /** schema.virtual("fullName").get(fn) */
  virtual(name: string): VirtualType {
    const have = this.virtuals[name];
    if (have != null) {
      return have;
    }
    const v = new VirtualType(name);
    this.virtuals[name] = v;
    return v;
  }

  /** an option: schema.set("autoIndex", false) */
  set(key: string, value: unknown): Schema<T> {
    this.opts[key] = value;
    return this;
  }

  /** an option's value (null when not set) */
  get(key: string): unknown {
    return this.opts[key] ?? null;
  }

  /** a copy (paths, options, indexes, virtuals): changing it leaves this one as it is */
  clone(): Schema<T> {
    const c = new Schema<T>(null, { _id: false });
    for (const [k, v] of Object.entries(this.paths)) {
      c.paths[k] = v;
    }
    for (const [k, v] of Object.entries(this.defs)) {
      c.defs[k] = v;
    }
    for (const [k, v] of Object.entries(this.opts)) {
      c.opts[k] = v;
    }
    for (const ix of this.own) {
      c.own.push(ix);
    }
    for (const [k, v] of Object.entries(this.virtuals)) {
      c.virtuals[k] = v;
    }
    return c;
  }

  /** schema.plugin(fn): applied now, as mongoose does when the model is compiled */
  plugin(fn: (schema: Schema<T>) => void): Schema<T> {
    this.plugins++;
    fn(this);
    return this;
  }
}

// ---------- casting by the schema ----------

/** the schema type at a path ("a", "a.b" through subdocuments), or null */
function pathAt(schema: Schema, path: string): SchemaType | null {
  const p = schema.path(path);
  if (p !== null) {
    return p;
  }
  const dot = path.indexOf(".");
  if (dot > 0) {
    const top = schema.paths[path.substring(0, dot)] ?? null;
    if (top !== null) {
      let sub = top.schema;
      const c = top.caster;
      if (sub === null && c !== null) sub = c.schema;
      if (sub !== null) {
        let rest = path.substring(dot + 1);
        if (/^\d+\./.test(rest)) rest = rest.substring(rest.indexOf(".") + 1); /* arr.0.x */
        return pathAt(sub, rest);
      }
    }
  }
  return null;
}

/** an object to walk (not an ObjectId, Date, array or bytes) */
function isPlainObject(v: unknown): boolean {
  if (v === null || typeof v !== "object") return false;
  if (v instanceof ObjectId || v instanceof Date || v instanceof Uint8Array || Array.isArray(v)) return false;
  return true;
}

/** the entries of an object, without the optional fields of a class that were never set (JSON.stringify leaves
 *  them out, as undefined is left out in Node) */
function entriesOf(v: unknown): [string, unknown][] {
  const rec = v as Record<string, unknown>;
  let present: Record<string, unknown> | null = null;
  for (const x of Object.values(rec)) {
    if (x === null) {
      present = JSON.parse(JSON.stringify(v));
      break;
    }
  }
  const out: [string, unknown][] = [];
  for (const [k, x] of Object.entries(rec)) {
    if (typeof x === "function") continue;
    if (x === null && present !== null && !(k in present)) continue;
    out.push([k, x]);
  }
  return out;
}

/** an object as a Record (null: an empty one) */
function recordOf(v: unknown): Record<string, unknown> {
  if (v === null) {
    const e: Record<string, unknown> = {};
    return e;
  }
  return v as Record<string, unknown>;
}

function applyStringSetters(st: SchemaType, s: string): string {
  let r = s;
  if (st.options.trim === true) r = r.trim();
  if (st.options.lowercase === true) r = r.toLowerCase();
  if (st.options.uppercase === true) r = r.toUpperCase();
  return r;
}

/** a single value cast to a path's type (ObjectId, Date, Number, String with its setters) */
function castScalar(st: SchemaType | null, v: unknown): unknown {
  if (st === null || v === null) {
    return v;
  }
  const t = st.instance;
  if (t === "ObjectId") {
    if (typeof v === "string" && isObjectIdOrHexString(v)) return new ObjectId(v as string);
    return v;
  }
  if (t === "Date") {
    if (typeof v === "string") {
      const d = new Date(v as string);
      if (Number.isNaN(d.getTime())) return v;
      return d;
    }
    if (typeof v === "number") return new Date(v as number);
    return v;
  }
  if (t === "Number") {
    if (typeof v === "string" && (v as string).trim().length > 0 && !Number.isNaN(Number(v as string))) return Number(v as string);
    if (typeof v === "boolean") return (v as boolean) ? 1 : 0;
    return v;
  }
  if (t === "String") {
    if (typeof v === "number") return applyStringSetters(st, String(v as number));
    if (typeof v === "boolean") return applyStringSetters(st, (v as boolean) ? "true" : "false");
    if (typeof v === "string") return applyStringSetters(st, v as string);
    return v;
  }
  if (t === "Boolean") {
    if (v === "true" || v === 1) return true;
    if (v === "false" || v === 0) return false;
    return v;
  }
  return v;
}

/** a value stored at a path: subdocuments and arrays cast through their schema */
function castForDoc(st: SchemaType | null, v: unknown, now: Date): unknown {
  if (st === null || v === null) {
    return v;
  }
  if (st.instance === "Embedded" && st.schema !== null && isPlainObject(v)) {
    return buildDoc(st.schema as Schema, v, false, now, false);
  }
  if (st.instance === "Array") {
    const c = st.caster;
    if (!Array.isArray(v)) {
      return [castForDoc(c, v, now)];
    }
    const out: unknown[] = [];
    for (const x of v as unknown[]) {
      if (c !== null && c.schema !== null && isPlainObject(x)) out.push(buildDoc(c.schema as Schema, x, false, now, false));
      else out.push(castScalar(c, x));
    }
    return out;
  }
  return castScalar(st, v);
}

/** a path's default (called when it is a function), or null when it has none.
 *  Node: `default: null` is the same as no default here (the option object cannot tell them apart), so such a path
 *  is left out of a new document instead of being stored as null (a { path: null } filter matches both). */
function defaultOf(st: SchemaType): unknown {
  const d = st.options.default ?? null;
  if (d !== null && typeof d === "function") {
    const f = d as () => unknown;
    return f();
  }
  return d;
}
/** an explicit default, or an array (arrays default to []) */
function hasDefault(st: SchemaType): boolean {
  return st.options.default != null || st.instance === "Array";
}
function explicitDefault(st: SchemaType): boolean {
  return st.options.default != null;
}

function emptyList(): unknown {
  const xs: unknown[] = [];
  return xs;
}

function isEmptyObject(v: unknown): boolean {
  return isPlainObject(v) && Object.keys(v as Record<string, unknown>).length === 0;
}

/** a document to insert, as mongoose builds it: the schema's paths in order (given values cast, then defaults),
 *  _id, the defaults of document arrays, timestamps and __v (create: createdAt, updatedAt, __v; insertMany: __v
 *  first); fields the schema does not have are left out (strict) */
function buildDoc(schema: Schema, input: unknown, top: boolean, now: Date, many: boolean): Record<string, unknown> {
  const given: Record<string, unknown> = {};
  for (const [k, v] of entriesOf(input)) {
    given[k] = v;
  }
  const out: Record<string, unknown> = {};
  const later: string[] = [];
  const stamps = schema.get("timestamps") === true;
  const minimize = schema.get("minimize") !== false;
  for (const [name, st] of Object.entries(schema.paths)) {
    if (stamps && (name === "createdAt" || name === "updatedAt")) continue;
    if (name in given) {
      const v = castForDoc(st, given[name], now);
      if (minimize && isEmptyObject(v)) continue;
      out[name] = v;
      continue;
    }
    if (name === "_id") {
      out["_id"] = new ObjectId();
      for (const l of later) out[l] = [];
      continue;
    }
    if (!hasDefault(st)) continue;
    const c = st.caster;
    if (st.instance === "Array" && !explicitDefault(st) && c !== null && c.schema !== null) {
      later.push(name); /* document arrays get theirs after _id */
      continue;
    }
    const d = st.instance === "Array" && !explicitDefault(st) ? emptyList() : defaultOf(st);
    if (d === null) continue;
    const v = castForDoc(st, d, now);
    if (minimize && isEmptyObject(v)) continue;
    out[name] = v;
  }
  if (!("_id" in schema.paths)) {
    for (const l of later) out[l] = [];
  }
  if (schema.get("strict") === false) {
    for (const [k, v] of Object.entries(given)) {
      if (!(k in out)) out[k] = v;
    }
  }
  if (top) {
    const vk = versionKeyOf(schema);
    if (many && vk !== null) out[vk] = 0;
    if (stamps) {
      out["createdAt"] = "createdAt" in given ? castScalar(schema.paths["createdAt"] ?? null, given["createdAt"]) : now;
      out["updatedAt"] = "updatedAt" in given ? castScalar(schema.paths["updatedAt"] ?? null, given["updatedAt"]) : now;
    }
    if (!many && vk !== null) out[vk] = 0;
  }
  return out;
}

function versionKeyOf(schema: Schema): string | null {
  const vk = schema.get("versionKey");
  if (vk === false || vk === null) return null;
  if (typeof vk === "string") return vk as string;
  return "__v";
}

const COMPARE_OPS = ["$eq", "$ne", "$gt", "$gte", "$lt", "$lte"];
const LIST_OPS = ["$in", "$nin", "$all"];

/** a condition on a path: a value, or { $op: value } with the values cast */
function castCondition(st: SchemaType | null, v: unknown): unknown {
  if (st === null || v === null) {
    return v;
  }
  const elem = st.instance === "Array" ? st.caster : st;
  if (isPlainObject(v)) {
    const keys = Object.keys(v as Record<string, unknown>);
    if (keys.length > 0 && keys[0].startsWith("$")) {
      const out: Record<string, unknown> = {};
      for (const [op, x] of Object.entries(v as Record<string, unknown>)) {
        if (COMPARE_OPS.includes(op)) out[op] = castScalar(elem, x);
        else if (LIST_OPS.includes(op) && Array.isArray(x)) {
          const xs: unknown[] = [];
          for (const e of x as unknown[]) xs.push(castScalar(elem, e));
          out[op] = xs;
        } else if (op === "$not") out[op] = castCondition(st, x);
        else out[op] = x;
      }
      return out;
    }
    return v;
  }
  if (Array.isArray(v)) {
    const xs: unknown[] = [];
    for (const e of v as unknown[]) xs.push(castScalar(elem, e));
    return xs;
  }
  return castScalar(elem, v);
}

/** a filter cast by the schema ($and/$or/$nor inside) */
function castFilter(schema: Schema, f: Record<string, unknown>): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const [k, v] of entriesOf(f)) {
    if ((k === "$and" || k === "$or" || k === "$nor") && Array.isArray(v)) {
      const xs: unknown[] = [];
      for (const e of v as unknown[]) xs.push(isPlainObject(e) ? castFilter(schema, e as Record<string, unknown>) : e);
      out[k] = xs;
    } else if (k.startsWith("$")) {
      out[k] = v;
    } else {
      out[k] = castCondition(pathAt(schema, k), v);
    }
  }
  return out;
}

/** the top-level keys a filter fixes by equality (they are not defaulted on an upsert) */
function equalityKeys(f: Record<string, unknown>): string[] {
  const keys: string[] = [];
  for (const k of Object.keys(f)) {
    if (!k.startsWith("$")) keys.push(k.indexOf(".") > 0 ? k.substring(0, k.indexOf(".")) : k);
  }
  return keys;
}

/** an update as mongoose sends it: fields without an operator go to $set, values cast, unknown paths left out
 *  (strict), timestamps ($set updatedAt, $setOnInsert createdAt) and, on an upsert, __v and the defaults of the
 *  paths neither the filter nor the update sets in $setOnInsert */
function buildUpdate(schema: Schema, update: Record<string, unknown>, filter: Record<string, unknown>, upsert: boolean, now: Date): Record<string, unknown> {
  const ops: Record<string, Record<string, unknown>> = {};
  const strict = schema.get("strict") !== false;
  const vkey = versionKeyOf(schema) ?? "";
  const put = (op: string, path: string, value: unknown): void => {
    const top = path.indexOf(".") > 0 ? path.substring(0, path.indexOf(".")) : path;
    if (strict && !(top in schema.paths) && op !== "$unset" && top !== vkey) {
      return;
    }
    const rec = ops[op] ?? null;
    if (rec !== null) {
      rec[path] = value;
    } else {
      const fresh: Record<string, unknown> = {};
      fresh[path] = value;
      ops[op] = fresh;
    }
  };
  for (const [k, v] of entriesOf(update)) {
    if (k.startsWith("$")) {
      if (!isPlainObject(v)) continue;
      for (const [path, x] of entriesOf(v)) {
        const st = pathAt(schema, path);
        if (k === "$set" || k === "$setOnInsert") put(k, path, castForDoc(st, x, now));
        else if (k === "$inc" || k === "$mul" || k === "$min" || k === "$max") put(k, path, castScalar(st, x));
        else if (k === "$push" || k === "$addToSet") {
          const elem = st !== null && st.instance === "Array" ? st.caster : st;
          if (isPlainObject(x) && "$each" in (x as Record<string, unknown>)) {
            const each: unknown[] = [];
            for (const e of ((x as Record<string, unknown>)["$each"] as unknown[])) each.push(castScalar(elem, e));
            put(k, path, { $each: each });
          } else {
            put(k, path, castScalar(elem, x));
          }
        } else if (k === "$pull") put(k, path, castCondition(st, x));
        else put(k, path, x);
      }
    } else {
      put("$set", k, castForDoc(pathAt(schema, k), v, now));
    }
  }
  const stamps = schema.get("timestamps") === true;
  if (stamps) {
    const set = ops["$set"] ?? null;
    if (set === null || !("updatedAt" in set)) put("$set", "updatedAt", now);
    put("$setOnInsert", "createdAt", now);
  }
  if (upsert) {
    const vk = versionKeyOf(schema);
    if (vk !== null) put("$setOnInsert", vk, 0);
    const taken = equalityKeys(filter);
    for (const rec of Object.values(ops)) {
      for (const p of Object.keys(rec)) taken.push(p.indexOf(".") > 0 ? p.substring(0, p.indexOf(".")) : p);
    }
    for (const [name, st] of Object.entries(schema.paths)) {
      if (name === "_id" || taken.includes(name) || (stamps && (name === "createdAt" || name === "updatedAt"))) continue;
      if (!hasDefault(st)) continue;
      const d = st.instance === "Array" && !explicitDefault(st) ? emptyList() : defaultOf(st);
      if (d === null) continue;
      const v = castForDoc(st, d, now);
      if (isEmptyObject(v)) continue;
      put("$setOnInsert", name, v);
    }
  }
  const out: Record<string, unknown> = {};
  for (const [op, rec] of Object.entries(ops)) {
    out[op] = rec;
  }
  return out;
}

/** select("a b -c") / { a: 1 }: the projection document */
function projectionOf(p: unknown): Record<string, unknown> {
  if (typeof p === "string") {
    const out: Record<string, unknown> = {};
    for (const part of (p as string).split(" ")) {
      if (part.length === 0 || part.startsWith("+")) continue;
      if (part.startsWith("-")) out[part.substring(1)] = 0;
      else out[part] = 1;
    }
    return out;
  }
  return p as Record<string, unknown>;
}

/** sort("-a b") / { a: -1, b: "asc" }: the sort document */
function sortOf(s: unknown): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  if (typeof s === "string") {
    for (const part of (s as string).split(" ")) {
      if (part.length === 0) continue;
      if (part.startsWith("-")) out[part.substring(1)] = -1;
      else out[part] = 1;
    }
    return out;
  }
  for (const [k, v] of entriesOf(s)) {
    if (v === "asc" || v === "ascending") out[k] = 1;
    else if (v === "desc" || v === "descending") out[k] = -1;
    else out[k] = v;
  }
  return out;
}

// ---------- queries and models ----------

/** a filter: { field: value, field: { $in: [...] }, $or: [...] } */
export type FilterQuery<T> = Record<string, unknown>;
/** an update: { $set: {...}, $setOnInsert: {...}, $inc: {...} } or the fields to set */
export type UpdateQuery<T> = Record<string, unknown>;
export type ProjectionType<T> = Record<string, unknown>;
export type RootFilterQuery<T> = Record<string, unknown>;

/** what find/findOne/findOneAndUpdate accept as options */
export interface QueryOptions<T = unknown> {
  upsert?: boolean;
  new?: boolean;
  lean?: boolean;
  sort?: Record<string, unknown>;
  skip?: number;
  limit?: number;
  projection?: Record<string, unknown>;
  setDefaultsOnInsert?: boolean;
  runValidators?: boolean;
  returnDocument?: string;
  includeResultMetadata?: boolean;
}

/** one operation of bulkWrite: { updateOne: { filter, update, upsert } }, { insertOne: { document } }... */
export interface AnyBulkWriteOperation<T = unknown> {
  insertOne?: Record<string, unknown>;
  updateOne?: Record<string, unknown>;
  updateMany?: Record<string, unknown>;
  replaceOne?: Record<string, unknown>;
  deleteOne?: Record<string, unknown>;
  deleteMany?: Record<string, unknown>;
}

export interface BulkWriteOptions {
  ordered?: boolean;
}

/** bulkWrite's result */
export class BulkWriteResult {
  insertedCount: number = 0;
  matchedCount: number = 0;
  modifiedCount: number = 0;
  deletedCount: number = 0;
  upsertedCount: number = 0;
}

/** deleteOne/deleteMany's result */
export class DeleteResult {
  acknowledged: boolean = true;
  deletedCount: number = 0;
}

/** updateOne/updateMany's result (in the order mongoose writes it) */
export class UpdateResult {
  acknowledged: boolean = true;
  modifiedCount: number = 0;
  upsertedId: ObjectId | null = null;
  upsertedCount: number = 0;
  matchedCount: number = 0;
}

/** what exists() gives when a document matches */
export interface ExistsResult {
  _id: ObjectId;
}

interface ValueReply {
  value?: object | null;
}

interface ValuesReply {
  values?: object;
}

interface CountReply {
  n?: number;
}

/** A query: built by a Model (find, findOne, countDocuments...), refined by sort/skip/limit/select/lean, run by
 *  exec() or by awaiting it. R is what it gives. */
export class Query<R, T = unknown> {
  /** the operation: find, findOne, countDocuments, findOneAndUpdate, deleteOne, distinct... */
  readonly op: string;
  readonly model: Model<T>;
  private filter: Record<string, unknown>;
  update: Record<string, unknown> | null = null;
  opts: Record<string, unknown> = {};
  field: string = "";
  isLean: boolean = false;

  constructor(model: Model<T>, op: string, filter: Record<string, unknown>) {
    this.model = model;
    this.op = op;
    this.filter = filter;
  }

  /** the filter (getFilter() in mongoose) */
  getFilter(): Record<string, unknown> {
    return this.filter;
  }
  getOptions(): Record<string, unknown> {
    return this.opts;
  }
  getUpdate(): Record<string, unknown> | null {
    return this.update;
  }

  /** @internal */
  withUpdate(update: Record<string, unknown>): Query<R, T> {
    this.update = update;
    return this;
  }
  /** @internal */
  withField(field: string): Query<R, T> {
    this.field = field;
    return this;
  }
  /** @internal */
  withOption(key: string, value: unknown): Query<R, T> {
    this.opts[key] = value;
    return this;
  }

  sort(sort: unknown): Query<R, T> {
    this.opts["sort"] = sort;
    return this;
  }
  skip(n: number): Query<R, T> {
    this.opts["skip"] = n;
    return this;
  }
  limit(n: number): Query<R, T> {
    this.opts["limit"] = n;
    return this;
  }
  /** the fields to return: "a b -c" or { a: 1 } */
  select(fields: unknown): Query<R, T> {
    this.opts["projection"] = fields;
    return this;
  }
  /** plain objects instead of documents (the same here: documents are plain objects of T) */
  lean<L = R>(): Query<L, T> {
    const q = new Query<L, T>(this.model, this.op, this.filter);
    q.update = this.update;
    q.opts = this.opts;
    q.field = this.field;
    q.isLean = true;
    return q;
  }
  populate(path: unknown): Query<R, T> {
    this.opts["populate"] = path;
    return this;
  }
  session(s: unknown): Query<R, T> {
    return this;
  }

  /** the command this query sends (filter and update as given; exec() casts them by the schema first) */
  toCommand(): Record<string, unknown> {
    return this.command(false, new Date());
  }

  private command(cast: boolean, now: Date): Record<string, unknown> {
    const cmd: Record<string, unknown> = {};
    const coll = this.model.collectionName;
    const schema = this.model.schema;
    const filter = cast ? castFilter(schema, this.filter) : this.filter;
    if (this.op === "find" || this.op === "findOne" || this.op === "findById" || this.op === "exists") {
      cmd["find"] = coll;
      cmd["filter"] = filter;
      if (this.opts["sort"] != null) cmd["sort"] = cast ? sortOf(this.opts["sort"]) : this.opts["sort"];
      if (this.opts["projection"] != null) cmd["projection"] = cast ? projectionOf(this.opts["projection"]) : this.opts["projection"];
      if (this.opts["skip"] != null) cmd["skip"] = this.opts["skip"];
      if (this.op !== "find") {
        cmd["limit"] = 1;
        if (cast) cmd["singleBatch"] = true;
      } else if (this.opts["limit"] != null) cmd["limit"] = this.opts["limit"];
    } else if (this.op === "countDocuments") {
      cmd["aggregate"] = coll;
      cmd["pipeline"] = [{ $match: filter }, { $group: { _id: 1, n: { $sum: 1 } } }];
      if (cast) cmd["cursor"] = {};
    } else if (this.op === "estimatedDocumentCount") {
      cmd["count"] = coll;
    } else if (this.op === "distinct") {
      cmd["distinct"] = coll;
      cmd["key"] = this.field;
      cmd["query"] = filter;
    } else if (this.op === "deleteOne" || this.op === "deleteMany") {
      cmd["delete"] = coll;
      cmd["deletes"] = [{ q: filter, limit: this.op === "deleteOne" ? 1 : 0 }];
      if (cast) cmd["ordered"] = true;
    } else if (this.op === "updateOne" || this.op === "updateMany") {
      const upsert = this.opts["upsert"] === true;
      const u = this.update ?? {};
      cmd["update"] = coll;
      const one: Record<string, unknown> = { q: filter, u: cast ? buildUpdate(schema, u, this.filter, upsert, now) : u };
      if (cast) {
        if (this.opts["upsert"] != null) one["upsert"] = upsert;
        if (this.op === "updateMany") one["multi"] = true;
      } else {
        one["multi"] = this.op === "updateMany";
        one["upsert"] = upsert;
      }
      cmd["updates"] = [one];
      if (cast) cmd["ordered"] = true;
    } else {
      // findOneAndUpdate, findOneAndDelete, findByIdAndDelete...
      const upsert = this.opts["upsert"] === true;
      cmd["findAndModify"] = coll;
      cmd["query"] = filter;
      const u = this.update;
      if (cast) {
        cmd["remove"] = u === null;
        cmd["new"] = this.opts["new"] === true || this.opts["returnDocument"] === "after";
        cmd["upsert"] = upsert;
        if (u !== null) cmd["update"] = buildUpdate(schema, u, this.filter, upsert, now);
        if (this.opts["sort"] != null) cmd["sort"] = sortOf(this.opts["sort"]);
        if (this.opts["projection"] != null) cmd["fields"] = projectionOf(this.opts["projection"]);
      } else {
        if (u !== null) cmd["update"] = u;
        else cmd["remove"] = true;
        if (this.opts["new"] === true) cmd["new"] = true;
        if (upsert) cmd["upsert"] = true;
        if (this.opts["sort"] != null) cmd["sort"] = this.opts["sort"];
      }
    }
    return cmd;
  }

  /** the result as JSON text, in the shape R has */
  private async run(): Promise<string> {
    const conn = this.model.db;
    await conn.__ready(this.model.collectionName, this.op);
    const cmd = this.command(true, new Date());
    const op = this.op;
    if (op === "find") {
      return conn.__cursor(cmd);
    }
    if (op === "findOne" || op === "findById" || op === "exists") {
      const docs: object[] = JSON.parse(conn.__cursor(cmd));
      const one = docs.length > 0 ? JSON.stringify(docs[0]) : "null";
      if (op !== "exists" && !this.isLean) rememberDocuments(one, this.model.connectionKey + "/" + this.model.modelName);
      return one;
    }
    if (op === "countDocuments") {
      const docs: CountReply[] = JSON.parse(conn.__cursor(cmd));
      return docs.length > 0 ? String(docs[0].n ?? 0) : "0";
    }
    if (op === "estimatedDocumentCount") {
      const r: CountReply = JSON.parse(conn.__text(cmd));
      return String(r.n ?? 0);
    }
    if (op === "distinct") {
      const r: ValuesReply = JSON.parse(conn.__text(cmd));
      const v = r.values;
      return v != null ? JSON.stringify(v) : "[]";
    }
    if (op === "deleteOne" || op === "deleteMany") {
      const w = writeReply(conn.__text(cmd));
      const d = new DeleteResult();
      d.deletedCount = w.n ?? 0;
      return JSON.stringify(d);
    }
    if (op === "updateOne" || op === "updateMany") {
      const w = writeReply(conn.__text(cmd));
      const u = new UpdateResult();
      const ups = w.upserted ?? [];
      u.upsertedCount = ups.length;
      u.matchedCount = (w.n ?? 0) - ups.length;
      u.modifiedCount = w.nModified ?? 0;
      if (ups.length > 0) u.upsertedId = ups[0]._id;
      return JSON.stringify(u);
    }
    const r: ValueReply = JSON.parse(conn.__text(cmd));
    const v = r.value ?? null;
    return v !== null ? JSON.stringify(v) : "null";
  }

  /** runs the query */
  async exec(): Promise<R> {
    const text = await this.run();
    const r: R = JSON.parse(text);
    return r;
  }

  /** await query: the same as await query.exec() */
  async then(): Promise<R> {
    return await this.exec();
  }

  /** query.catch(fn): exec(), with fn on a failure */
  async catch(fn: (err: Error) => R): Promise<R> {
    try {
      return await this.exec();
    } catch (e) {
      return fn(e as Error);
    }
  }
}

/** model.aggregate(pipeline): awaited or exec()'d, the documents (typed by aggregate<A>()) */
export class Aggregate<R> {
  private readonly conn: Connection;
  private readonly cmd: Record<string, unknown>;
  private readonly collectionName: string;
  constructor(conn: Connection, collectionName: string, pipeline: unknown[]) {
    this.conn = conn;
    this.collectionName = collectionName;
    this.cmd = { aggregate: collectionName, pipeline: pipeline, cursor: {} };
  }
  /** the pipeline */
  pipeline(): unknown[] {
    return this.cmd["pipeline"] as unknown[];
  }
  async exec(): Promise<R> {
    await this.conn.__ready(this.collectionName, "aggregate");
    const r: R = JSON.parse(this.conn.__cursor(this.cmd));
    return r;
  }
  async then(): Promise<R> {
    return await this.exec();
  }
  async catch(fn: (err: Error) => R): Promise<R> {
    try {
      return await this.exec();
    } catch (e) {
      return fn(e as Error);
    }
  }
}

/** A collection of a connection (model.collection) */
export class Collection {
  readonly collectionName: string;
  readonly conn: Connection;
  constructor(conn: Connection, name: string) {
    this.conn = conn;
    this.collectionName = name;
  }
  get name(): string {
    return this.collectionName;
  }
}

// connections and schemas by key: a Model holds only names (its state is JSON, so a Nest constructor parameter
// Model<X> is made from it as the exact instance it declares), and finds its connection and schema here
const connectionsByKey: Map<string, Connection> = new Map<string, Connection>();
const schemasByKey: Map<string, Schema> = new Map<string, Schema>();
let connectionSeq: int = 0;

/** @internal registers a connection under a key (MongooseModule: "nest:<connection name>") */
export function __registerConnection(key: string, conn: Connection): void {
  connectionsByKey.set(key, conn);
}

/** @internal the state of a model, as JSON (what Model<T> is decoded from) */
export function __modelState(connectionKey: string, name: string, schema: Schema, collection: string = ""): string {
  schemasByKey.set(connectionKey + "/" + name, schema);
  const coll = collection.length > 0 ? collection : ((schema.get("collection") as string | null) ?? pluralize(name));
  return JSON.stringify({ modelName: name, collectionName: coll, connectionKey: connectionKey });
}

const uncountables = ["advice", "energy", "excretion", "digestion", "cooperation", "health", "justice", "labour", "machinery", "equipment",
  "information", "pollution", "sewage", "paper", "money", "species", "series", "rain", "rice", "fish", "sheep", "moose", "deer",
  "news", "expertise", "status", "media"];

interface PluralRule {
  re: RegExp;
  to: string;
}

const pluralRules: PluralRule[] = [
  { re: /human$/gi, to: "humans" },
  { re: /(m)an$/gi, to: "$1en" },
  { re: /(pe)rson$/gi, to: "$1ople" },
  { re: /(child)$/gi, to: "$1ren" },
  { re: /^(ox)$/gi, to: "$1en" },
  { re: /(ax|test)is$/gi, to: "$1es" },
  { re: /(octop|vir)us$/gi, to: "$1i" },
  { re: /(alias|status)$/gi, to: "$1es" },
  { re: /(bu)s$/gi, to: "$1ses" },
  { re: /(buffal|tomat|potat)o$/gi, to: "$1oes" },
  { re: /([ti])um$/gi, to: "$1a" },
  { re: /sis$/gi, to: "ses" },
  { re: /(?:([^f])fe|([lr])f)$/gi, to: "$1$2ves" },
  { re: /(hive)$/gi, to: "$1s" },
  { re: /([^aeiouy]|qu)y$/gi, to: "$1ies" },
  { re: /(x|ch|ss|sh)$/gi, to: "$1es" },
  { re: /(matr|vert|ind)ix|ex$/gi, to: "$1ices" },
  { re: /([m|l])ouse$/gi, to: "$1ice" },
  { re: /(kn|w|l)ife$/gi, to: "$1ives" },
  { re: /(quiz)$/gi, to: "$1zes" },
  { re: /^goose$/i, to: "geese" },
  { re: /s$/gi, to: "s" },
  { re: /([^a-z])$/, to: "$1" },
  { re: /$/gi, to: "s" },
];

/** the collection name mongoose gives a model ("KioskUpdateGroup" -> "kioskupdategroups") */
export function pluralize(name: string): string {
  const s = name.toLowerCase();
  if (uncountables.includes(s)) {
    return s;
  }
  for (const r of pluralRules) {
    r.re.lastIndex = 0;
    if (r.re.test(s)) {
      r.re.lastIndex = 0;
      return s.replace(r.re, r.to);
    }
  }
  return s;
}

/** A model: the operations on one collection, documents of type T. */
export class Model<T = unknown> {
  modelName: string = "";
  collectionName: string = "";
  /** the key of its connection (see __registerConnection) */
  connectionKey: string = "";

  /** the connection (it fails when its connection was never made, as a Nest model without MongooseModule.forRoot) */
  get db(): Connection {
    const c = connectionsByKey.get(this.connectionKey);
    if (c === null) {
      throw new MongooseError("the connection of model " + this.modelName + " was not opened");
    }
    return c;
  }
  get schema(): Schema {
    return schemasByKey.get(this.connectionKey + "/" + this.modelName) ?? new Schema();
  }
  get collection(): Collection {
    return new Collection(this.db, this.collectionName);
  }

  find(filter: FilterQuery<T> = {}, projection: unknown = null, options: QueryOptions<T> | null = null): Query<T[], T> {
    const q = new Query<T[], T>(this, "find", filter);
    if (projection !== null) q.select(projection);
    if (options !== null) {
      if (options.sort != null) q.sort(options.sort);
      if (options.skip != null) q.skip(options.skip);
      if (options.limit != null) q.limit(options.limit);
      if (options.projection != null) q.select(options.projection);
    }
    return q;
  }
  findOne(filter: FilterQuery<T> = {}, projection: unknown = null, options: QueryOptions<T> | null = null): Query<T | null, T> {
    const q = new Query<T | null, T>(this, "findOne", filter);
    if (projection !== null) q.select(projection);
    if (options !== null) {
      if (options.sort != null) q.sort(options.sort);
      if (options.skip != null) q.skip(options.skip);
    }
    return q;
  }
  findById(id: unknown, projection: unknown = null): Query<T | null, T> {
    const f: Record<string, unknown> = {};
    f["_id"] = id;
    const q = new Query<T | null, T>(this, "findById", f);
    if (projection !== null) q.select(projection);
    return q;
  }
  findOneAndUpdate(filter: FilterQuery<T>, update: UpdateQuery<T>, options: QueryOptions<T> | null = null): Query<T | null, T> {
    const q = new Query<T | null, T>(this, "findOneAndUpdate", filter).withUpdate(update);
    if (options !== null) {
      if (options.new != null) q.withOption("new", options.new);
      if (options.returnDocument != null) q.withOption("returnDocument", options.returnDocument);
      if (options.upsert != null) q.withOption("upsert", options.upsert);
      if (options.sort != null) q.sort(options.sort);
      if (options.projection != null) q.select(options.projection);
    }
    return q;
  }
  findByIdAndUpdate(id: unknown, update: UpdateQuery<T>, options: QueryOptions<T> | null = null): Query<T | null, T> {
    const f: Record<string, unknown> = {};
    f["_id"] = id;
    return this.findOneAndUpdate(f, update, options);
  }
  findOneAndDelete(filter: FilterQuery<T>, options: QueryOptions<T> | null = null): Query<T | null, T> {
    const q = new Query<T | null, T>(this, "findOneAndDelete", filter);
    if (options !== null && options.sort != null) q.sort(options.sort);
    return q;
  }
  findByIdAndDelete(id: unknown): Query<T | null, T> {
    const f: Record<string, unknown> = {};
    f["_id"] = id;
    return new Query<T | null, T>(this, "findByIdAndDelete", f);
  }
  countDocuments(filter: FilterQuery<T> = {}): Query<number, T> {
    return new Query<number, T>(this, "countDocuments", filter);
  }
  estimatedDocumentCount(): Query<number, T> {
    return new Query<number, T>(this, "estimatedDocumentCount", {});
  }
  exists(filter: FilterQuery<T>): Query<ExistsResult | null, T> {
    return new Query<ExistsResult | null, T>(this, "exists", filter).select({ _id: 1 });
  }
  distinct(field: string, filter: FilterQuery<T> = {}): Query<unknown[], T> {
    return new Query<unknown[], T>(this, "distinct", filter).withField(field);
  }
  deleteOne(filter: FilterQuery<T> = {}): Query<DeleteResult, T> {
    return new Query<DeleteResult, T>(this, "deleteOne", filter);
  }
  deleteMany(filter: FilterQuery<T> = {}): Query<DeleteResult, T> {
    return new Query<DeleteResult, T>(this, "deleteMany", filter);
  }
  updateOne(filter: FilterQuery<T>, update: UpdateQuery<T>, options: QueryOptions<T> | null = null): Query<UpdateResult, T> {
    const q = new Query<UpdateResult, T>(this, "updateOne", filter).withUpdate(update);
    if (options !== null && options.upsert != null) q.withOption("upsert", options.upsert);
    return q;
  }
  updateMany(filter: FilterQuery<T>, update: UpdateQuery<T>, options: QueryOptions<T> | null = null): Query<UpdateResult, T> {
    const q = new Query<UpdateResult, T>(this, "updateMany", filter).withUpdate(update);
    if (options !== null && options.upsert != null) q.withOption("upsert", options.upsert);
    return q;
  }

  /** inserts a document (its fields, cast by the schema, with defaults, _id, timestamps and __v): the document */
  async create(doc: unknown): Promise<T> {
    const conn = this.db;
    await conn.__ready(this.collectionName, "insertOne");
    const built = buildDoc(this.schema, doc, true, new Date(), false);
    writeReply(conn.__text({ insert: this.collectionName, documents: [built], ordered: true }));
    const text = JSON.stringify(built);
    rememberDocuments(text, this.connectionKey + "/" + this.modelName);
    const r: T = JSON.parse(text);
    return r;
  }
  /** inserts the documents in one command (ordered): the documents */
  async insertMany(docs: unknown[]): Promise<T[]> {
    const conn = this.db;
    await conn.__ready(this.collectionName, "insertMany");
    const now = new Date();
    const built: Record<string, unknown>[] = [];
    for (const d of docs) {
      built.push(buildDoc(this.schema, d, true, now, true));
    }
    if (built.length > 0) {
      const text = conn.__text({ insert: this.collectionName, documents: built, ordered: true });
      try {
        writeReply(text);
      } catch (e) {
        if (e instanceof MongoServerError) {
          const b = new MongoBulkWriteError(e.message);
          b.code = e.code;
          b.errmsg = e.errmsg;
          b.errorResponse = e.errorResponse;
          b.keyPattern = e.keyPattern;
          b.keyValue = e.keyValue;
          b.index = e.index;
          throw b;
        }
        throw e;
      }
    }
    const r: T[] = JSON.parse(JSON.stringify(built));
    return r;
  }
  /** several writes ({ insertOne: { document } }, { updateOne: { filter, update, upsert } }, { deleteMany: {
   *  filter } }...), one command each, in order (ordered: false goes on after a failed one): the counts */
  async bulkWrite(ops: unknown[], options: BulkWriteOptions | null = null): Promise<BulkWriteResult> {
    const conn = this.db;
    await conn.__ready(this.collectionName, "bulkWrite");
    const ordered = options === null || options.ordered !== false;
    const now = new Date();
    const res = new BulkWriteResult();
    const schema = this.schema;
    let firstError: Error | null = null;
    for (const raw of ops) {
      for (const [kind, v] of entriesOf(raw)) {
        const spec = recordOf(v);
        try {
          if (kind === "insertOne") {
            const doc = buildDoc(schema, recordOf(spec["document"] ?? null), true, now, true);
            const w = writeReply(conn.__text({ insert: this.collectionName, documents: [doc], ordered: true }));
            res.insertedCount += w.n ?? 0;
          } else if (kind === "updateOne" || kind === "updateMany" || kind === "replaceOne") {
            const filter = recordOf(spec["filter"] ?? null);
            const upsert = spec["upsert"] === true;
            const u = kind === "replaceOne" ? buildDoc(schema, recordOf(spec["replacement"] ?? null), true, now, false)
              : buildUpdate(schema, recordOf(spec["update"] ?? null), filter, upsert, now);
            const one: Record<string, unknown> = { q: castFilter(schema, filter), u: u };
            if (upsert) one["upsert"] = true;
            if (kind === "updateMany") one["multi"] = true;
            const w = writeReply(conn.__text({ update: this.collectionName, updates: [one], ordered: true }));
            const ups = w.upserted ?? [];
            res.upsertedCount += ups.length;
            res.matchedCount += (w.n ?? 0) - ups.length;
            res.modifiedCount += w.nModified ?? 0;
          } else if (kind === "deleteOne" || kind === "deleteMany") {
            const filter = recordOf(spec["filter"] ?? null);
            const w = writeReply(conn.__text({ delete: this.collectionName, deletes: [{ q: castFilter(schema, filter), limit: kind === "deleteOne" ? 1 : 0 }], ordered: true }));
            res.deletedCount += w.n ?? 0;
          }
        } catch (e) {
          if (ordered) throw e;
          if (firstError === null) firstError = e as Error;
        }
      }
    }
    const fe = firstError;
    if (fe !== null) throw fe;
    return res;
  }
  /** an aggregation pipeline (aggregate<A>() types the documents): awaited or exec()'d */
  aggregate<A = unknown>(pipeline: unknown[]): Aggregate<A[]> {
    return new Aggregate<A[]>(this.db, this.collectionName, pipeline);
  }

  /** createCollection (autoCreate) and the indexes (autoIndex), as when the model is made on an open connection */
  async init(): Promise<void> {
    this.db.__initModel(this.modelName, this.schema, this.collectionName);
  }
  /** creates the schema's indexes */
  async createIndexes(): Promise<void> {
    this.db.__createIndexes(this.schema, this.collectionName);
  }
  async ensureIndexes(): Promise<void> {
    this.db.__createIndexes(this.schema, this.collectionName);
  }
  /** creates the collection (an existing one is not an error) */
  async createCollection(): Promise<void> {
    this.db.__createCollection(this.collectionName);
  }
}

/** A hydrated document: T itself for now (mongoose: T & Document; the compiler branch that gives intersections
 *  with a class their members makes it T & Document) */
export type HydratedDocument<T> = T;

// the model (its connection key and name) of each document create/findOne/findById gave, by _id: what save() writes
// through (a document decoded from JSON carries no link of its own, and a field for it would show in its JSON)
const documentModels: Map<string, string> = new Map<string, string>();

interface IdOnly {
  _id?: ObjectId;
}

function rememberDocuments(text: string, modelKey: string): void {
  if (text === "null") {
    return;
  }
  const one: IdOnly = JSON.parse(text);
  const id = one._id ?? null;
  if (id !== null) {
    if (documentModels.size > 10000) documentModels.clear();
    documentModels.set(id.toHexString(), modelKey);
  }
}

/** mongoose's Document members: _id, __v, toObject(), toJSON(), save() (what HydratedDocument<T> adds to T) */
export class Document {
  _id: ObjectId = new ObjectId();
  __v?: number;

  /** the document as a plain object (its fields, ObjectIds and Dates as they are) */
  toObject(): Record<string, unknown> {
    const out: Record<string, unknown> = {};
    for (const [k, v] of entriesOf(this)) {
      out[k] = v;
    }
    return out;
  }

  /** writes the document: its fields ($set) on its _id, upserted; the document */
  async save(): Promise<Document> {
    const key = documentModels.get(this._id.toHexString());
    if (key === null) {
      throw new MongooseError("save(): this document did not come from a model (create, findOne or findById)");
    }
    const slash = key.lastIndexOf("/");
    const conn = connectionsByKey.get(key.substring(0, slash));
    const schema = schemasByKey.get(key);
    if (conn === null || schema === null) {
      throw new MongooseError("save(): the model of this document is gone");
    }
    const st: Model<unknown> = JSON.parse(__modelState(key.substring(0, slash), key.substring(slash + 1), schema));
    const set: Record<string, unknown> = {};
    for (const [k, v] of entriesOf(this)) {
      if (k !== "_id" && k !== "__v" && k !== "createdAt") set[k] = v;
    }
    const f: Record<string, unknown> = { _id: this._id };
    const update = buildUpdate(schema, { $set: set }, f, true, new Date());
    writeReply(conn.__text({ update: st.collectionName, updates: [{ q: f, u: update, upsert: true }], ordered: true }));
    return this;
  }
}

// ---------- connections ----------

/** mongoose.ConnectionStates / connection.readyState */
export enum ConnectionStates {
  disconnected = 0,
  connected = 1,
  connecting = 2,
  disconnecting = 3,
  uninitialized = 99,
}

/** createConnection / connect options */
export interface ConnectOptions {
  dbName?: string;
  user?: string;
  pass?: string;
  authSource?: string;
  autoIndex?: boolean;
  autoCreate?: boolean;
  bufferCommands?: boolean;
  serverSelectionTimeoutMS?: number;
  connectTimeoutMS?: number;
  socketTimeoutMS?: number;
  maxPoolSize?: number;
  minPoolSize?: number;
  appName?: string;
  tls?: boolean;
  replicaSet?: string;
  directConnection?: boolean;
  family?: number;
}

interface Listener {
  event: string;
  once: boolean;
  fn: (arg: unknown) => void;
}

interface PendingModel {
  name: string;
  schema: Schema;
  collection: string;
}

/** A connection (createConnection, or MongooseModule's): models, state, events */
export class Connection {
  /** the database name */
  name: string;
  /** the host and port it connected to (the URI's first host before) */
  host: string;
  port: number = 0;
  private state: ConnectionStates = ConnectionStates.disconnected;
  /** the models made on this connection: their state, by name */
  private modelStates: Record<string, string> = {};
  readonly uri: string;
  readonly options: ConnectOptions;
  private listeners: Listener[] = [];
  private client: MongoClient | null = null;
  private dbs: Record<string, Connection> = {};
  private parent: Connection | null = null;
  private pending: PendingModel[] = [];
  private opening: boolean = false;
  /** its key in the registry of connections */
  readonly key: string;

  constructor(uri: string = "", options: ConnectOptions = {}) {
    connectionSeq++;
    this.key = "c" + String(connectionSeq);
    connectionsByKey.set(this.key, this);
    this.uri = uri;
    this.options = options;
    this.name = options.dbName ?? dbNameOf(uri);
    this.host = hostOf(uri);
  }

  /** 0 disconnected, 1 connected, 2 connecting, 3 disconnecting (a useDb connection: its parent's) */
  get readyState(): ConnectionStates {
    const p = this.parent;
    return p !== null ? p.readyState : this.state;
  }

  /** @internal */
  __setState(s: ConnectionStates): void {
    this.state = s;
  }

  /** connection.db: the database (null until connected) */
  get db(): Db | null {
    if (this.readyState !== ConnectionStates.connected) {
      return null;
    }
    return this.getClient().db(this.name);
  }

  /** the driver's MongoClient: options (hosts, credentials, dbName...) */
  getClient(): MongoClient {
    const p = this.parent;
    if (p !== null) {
      return p.getClient();
    }
    const have = this.client;
    if (have !== null) {
      return have;
    }
    const c = new MongoClient(this.uri, clientOptions(this.options));
    this.client = c;
    return c;
  }

  /** opens the connection: server selection through the driver (MongooseServerSelectionError when it fails) */
  async openUri(uri: string = "", options: ConnectOptions | null = null): Promise<Connection> {
    const target = uri.length > 0 ? uri : this.uri;
    const opts = options ?? this.options;
    this.state = ConnectionStates.connecting;
    this.opening = true;
    let client: MongoClient;
    try {
      client = new MongoClient(target, clientOptions(opts));
    } catch (e) {
      this.state = ConnectionStates.disconnected;
      this.opening = false;
      throw e;
    }
    this.client = client;
    try {
      await client.connect();
    } catch (e) {
      this.state = ConnectionStates.disconnected;
      this.opening = false;
      this.emit("disconnected", null);
      this.emit("error", e);
      if (e instanceof MongoServerSelectionError) {
        throw new MongooseServerSelectionError(e.message);
      }
      throw e;
    }
    this.state = ConnectionStates.connected;
    this.opening = false;
    const hosts = client.options.hosts;
    if (hosts.length > 0) {
      this.host = hosts[0].host;
      this.port = hosts[0].port;
    }
    this.emit("connected", null);
    this.emit("open", null);
    const waiting = this.pending;
    this.pending = [];
    for (const m of waiting) {
      this.__initModel(m.name, m.schema, m.collection);
    }
    for (const child of Object.values(this.dbs)) {
      const w = child.pending;
      child.pending = [];
      for (const m of w) {
        child.__initModel(m.name, m.schema, m.collection);
      }
    }
    return this;
  }

  /** await connection.asPromise(): the connection, once open */
  async asPromise(): Promise<Connection> {
    if (this.readyState === ConnectionStates.connected) {
      return this;
    }
    return await this.openUri();
  }

  async close(force: boolean = false): Promise<void> {
    const was = this.state;
    const c = this.client;
    if (c !== null && was === ConnectionStates.connected) {
      this.state = ConnectionStates.disconnecting;
      await c.close(force);
    }
    this.state = ConnectionStates.disconnected;
    if (was !== ConnectionStates.disconnected) {
      this.emit("disconnected", null);
    }
    this.emit("close", null);
  }

  /** the driver's client, open (the parent's for a useDb connection) */
  private openClient(): MongoClient {
    if (this.readyState !== ConnectionStates.connected) {
      throw new MongooseError("Connection " + this.name + " is not open");
    }
    return this.getClient();
  }

  /** runs one command on this connection's database: the reply */
  async runCommand(command: Record<string, unknown>): Promise<Record<string, unknown>> {
    const r: Record<string, unknown> = JSON.parse(this.__text(command));
    return r;
  }

  /** @internal a command's reply as JSON text (MongoServerError when it failed) */
  __text(command: Record<string, unknown>): string {
    return this.openClient().commandText(this.name, command);
  }
  /** @internal a cursor command's documents as JSON text (an array) */
  __cursor(command: Record<string, unknown>): string {
    return this.openClient().cursorText(this.name, command);
  }

  /** @internal before an operation: an open connection; with bufferCommands (the default) it waits up to 10 s for
   *  one being opened, as mongoose buffers the operation */
  async __ready(collection: string, op: string): Promise<void> {
    if (this.readyState === ConnectionStates.connected) {
      return;
    }
    if (this.options.bufferCommands === false) {
      throw new MongooseError("Cannot call `" + collection + "." + op + "()` before initial connection is complete if `bufferCommands = false`. Make sure you `await mongoose.connect()` if you have `bufferCommands = false`.");
    }
    const until = Date.now() + 10000;
    const p = this.parent ?? this;
    if (p.state === ConnectionStates.connecting && !p.opening) {
      // createConnection(uri) opens in the background in Node: here, the first operation opens it
      try {
        await p.openUri();
        return;
      } catch (e) {
        /* the operation waits out its buffering time, as in Node */
      }
    }
    while (Date.now() < until) {
      await new Promise<void>((r) => setTimeout(r, 20));
      if (this.readyState === ConnectionStates.connected) {
        return;
      }
    }
    throw new MongooseError("Operation `" + collection + "." + op + "()` buffering timed out after 10000ms");
  }

  /** @internal createCollection (autoCreate) and the indexes (autoIndex) of a model; failures are left to the
   *  operations (mongoose reports them as events, not as errors of the application) */
  __initModel(name: string, schema: Schema, collection: string): void {
    const ac = schema.get("autoCreate") ?? this.options.autoCreate ?? null;
    if (ac !== false) {
      this.__createCollection(collection);
    }
    const ai = schema.get("autoIndex") ?? this.options.autoIndex ?? null;
    if (ai !== false) {
      this.__createIndexes(schema, collection);
    }
  }

  /** @internal */
  __createCollection(collection: string): void {
    try {
      this.__text({ create: collection });
    } catch (e) {
      /* NamespaceExists (48): already there */
    }
  }

  /** @internal one createIndexes command per index, as mongoose sends them */
  __createIndexes(schema: Schema, collection: string): void {
    for (const ix of schema.indexes()) {
      const fields = ix[0] as Record<string, unknown>;
      const opts = ix[1] as Record<string, unknown>;
      const spec: Record<string, unknown> = {};
      for (const [k, v] of Object.entries(opts)) {
        spec[k] = v;
      }
      let nm = "";
      for (const [k, v] of Object.entries(fields)) {
        nm += (nm.length > 0 ? "_" : "") + k + "_" + String(v);
      }
      if (!("name" in spec)) spec["name"] = nm;
      spec["key"] = fields;
      try {
        this.__text({ createIndexes: collection, indexes: [spec] });
      } catch (e) {
        /* (mongoose emits it as the model's 'index' event) */
      }
    }
  }

  /** connection.model(name, schema, collection?): the model (made once per name; initialised when the connection
   *  is open, or once it opens) */
  model<T = unknown>(name: string, schema: Schema | null = null, collection: string = ""): Model<T> {
    const have = this.modelStates[name];
    if (have != null && schema === null) {
      const m: Model<T> = JSON.parse(have);
      return m;
    }
    if (schema === null) {
      throw new MongooseError("Schema hasn't been registered for model \"" + name + "\".\nUse mongoose.model(name, schema)");
    }
    const fresh = have == null;
    const state = __modelState(this.key, name, schema, collection);
    this.modelStates[name] = state;
    const m: Model<T> = JSON.parse(state);
    if (fresh) {
      if (this.readyState === ConnectionStates.connected) {
        this.__initModel(name, schema, m.collectionName);
      } else {
        this.pending.push({ name: name, schema: schema, collection: m.collectionName });
      }
    }
    return m;
  }

  /** the model names */
  modelNames(): string[] {
    return Object.keys(this.modelStates);
  }

  /** another database on the same connection (useCache: the same object for the same name) */
  useDb(name: string, options: Record<string, unknown> = {}): Connection {
    const have = this.dbs[name];
    if (have != null) {
      return have;
    }
    const c = new Connection(this.uri, this.options);
    c.name = name;
    c.parent = this.parent ?? this;
    this.dbs[name] = c;
    return c;
  }

  on(event: string, fn: (arg: unknown) => void): Connection {
    this.listeners.push({ event: event, once: false, fn: fn });
    return this;
  }
  once(event: string, fn: (arg: unknown) => void): Connection {
    this.listeners.push({ event: event, once: true, fn: fn });
    return this;
  }
  /** calls the listeners of `event` (once listeners are removed) */
  emit(event: string, arg: unknown): boolean {
    let any = false;
    const keep: Listener[] = [];
    const run: Listener[] = [];
    for (const l of this.listeners) {
      if (l.event === event) {
        run.push(l);
        if (!l.once) keep.push(l);
      } else {
        keep.push(l);
      }
    }
    this.listeners = keep;
    for (const l of run) {
      any = true;
      l.fn(arg);
    }
    return any;
  }
}

function clientOptions(o: ConnectOptions): MongoClientOptions {
  const out: MongoClientOptions = {};
  if (o.user != null) out.user = o.user;
  if (o.pass != null) out.pass = o.pass;
  if (o.authSource != null) out.authSource = o.authSource;
  if (o.serverSelectionTimeoutMS != null) out.serverSelectionTimeoutMS = o.serverSelectionTimeoutMS;
  if (o.connectTimeoutMS != null) out.connectTimeoutMS = o.connectTimeoutMS;
  if (o.socketTimeoutMS != null) out.socketTimeoutMS = o.socketTimeoutMS;
  if (o.maxPoolSize != null) out.maxPoolSize = o.maxPoolSize;
  if (o.appName != null) out.appName = o.appName;
  if (o.tls != null) out.tls = o.tls;
  if (o.replicaSet != null) out.replicaSet = o.replicaSet;
  if (o.directConnection != null) out.directConnection = o.directConnection;
  return out;
}

/** mongoose's base error */
export class MongooseError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "MongooseError";
  }
}

/** the connection could not select a server: the driver's last error as the message */
export class MongooseServerSelectionError extends MongooseError {
  constructor(message: string) {
    super(message);
    this.name = "MongooseServerSelectionError";
  }
}

function hostOf(uri: string): string {
  const at = uri.indexOf("@");
  const start = uri.indexOf("://");
  let rest = start >= 0 ? uri.substring(start + 3) : uri;
  if (at >= 0) rest = uri.substring(at + 1);
  const slash = rest.indexOf("/");
  const hosts = slash >= 0 ? rest.substring(0, slash) : rest;
  const q = hosts.indexOf("?");
  return q >= 0 ? hosts.substring(0, q) : hosts;
}

function dbNameOf(uri: string): string {
  const start = uri.indexOf("://");
  if (start < 0) {
    return "test";
  }
  const rest = uri.substring(start + 3);
  const slash = rest.indexOf("/");
  if (slash < 0) {
    return "test";
  }
  let db = rest.substring(slash + 1);
  const q = db.indexOf("?");
  if (q >= 0) db = db.substring(0, q);
  return db.length > 0 ? db : "test";
}

/** mongoose.createConnection(uri, options): the connection, opening (asPromise() waits for it) */
export function createConnection(uri: string = "", options: ConnectOptions = {}): Connection {
  const c = new Connection(uri, options);
  if (uri.length > 0) {
    c.__setState(ConnectionStates.connecting);
  }
  return c;
}
