// mongoose (v8) for xisto, stage 1: the API without a MongoDB driver.
//
// What works without a server, as mongoose does: ObjectId (12 bytes: a 4-byte timestamp, 5 random bytes per process
// and a 3-byte counter, as the bson package makes them; hex, JSON, equals, getTimestamp, isValid), Schema (paths,
// options, indexes, clone, set/get, virtuals), Model and Query objects (the chain sort/skip/limit/select/lean is
// built and awaitable), Connection (readyState, name, models, useDb, model, events) and createConnection.
//
// What needs the server rejects with an Error whose message starts with NO_DRIVER: every Model/Query operation
// (find, findOne, create, countDocuments, bulkWrite...), Connection.asPromise()/openUri() and a Nest
// MongooseModule.forRoot connection. Only when it is used: a program that never opens a connection or runs an
// operation compiles and runs (an app that registers its Mongo modules only when MONGO_URI is set boots without it).
//
// Stage 2 (the driver: OP_MSG, BSON, SCRAM, pool) plugs in behind one seam: every operation is turned into a
// MongoDB command document (find, insert, update, delete, count, distinct, findAndModify, aggregate) and sent with
// Connection.runCommand(command), which asks the driver set with setMongoDriver(). Without one it rejects.
// Node: top-level `ObjectId` is the bson class here (in mongoose it is the SchemaType Schema.Types.ObjectId);
// `new Types.ObjectId(x)` is written `new ObjectId(x)` (xisto has no namespaces as values yet; Types.ObjectId as a
// type and Types.ObjectId.isValid/createFromHexString work).
import { ClassInfo } from "xisto:reflect";

export const NO_DRIVER = "mongoose: no MongoDB driver in xisto yet (stage 2)";

// ---------- ObjectId ----------

const HEX_DIGITS = "0123456789abcdef";

function hexByte(n: int): string {
  return HEX_DIGITS.charAt((n >> 4) & 15) + HEX_DIGITS.charAt(n & 15);
}

function isHex24(s: string): boolean {
  if (s.length !== 24) {
    return false;
  }
  return /^[0-9a-fA-F]{24}$/.test(s);
}

let processUnique = "";
let oidCounter: int = -1;

function randomByte(): int {
  return Math.floor(Math.random() * 256);
}

/** the 24 hex digits of a new id for `seconds` (bson: timestamp, process-unique random, counter) */
function generateHex(seconds: number): string {
  if (processUnique.length === 0) {
    for (let i = 0; i < 5; i++) {
      processUnique += hexByte(randomByte());
    }
    oidCounter = randomByte() * 65536 + randomByte() * 256 + randomByte();
  }
  oidCounter = (oidCounter + 1) % 16777216;
  const t: int = Math.trunc(seconds) % 4294967296;
  let out = "";
  out += hexByte(Math.trunc(t / 16777216) % 256) + hexByte(Math.trunc(t / 65536) % 256) + hexByte(Math.trunc(t / 256) % 256) + hexByte(t % 256);
  out += processUnique;
  out += hexByte(Math.trunc(oidCounter / 65536) % 256) + hexByte(Math.trunc(oidCounter / 256) % 256) + hexByte(oidCounter % 256);
  return out;
}

/** bson's ObjectId: a 12-byte id, written as 24 hex digits */
export class ObjectId {
  private readonly hex: string = "";

  /** new ObjectId(): a new id; new ObjectId("24 hex digits"); new ObjectId(seconds): a new id for that time */
  constructor(id: unknown = null) {
    if (id === null) {
      this.hex = generateHex(Date.now() / 1000);
    } else if (typeof id === "string") {
      const s = id as string;
      if (!isHex24(s)) {
        throw new BSONError("input must be a 24 character hex string, 12 byte Uint8Array, or an integer");
      }
      this.hex = s.toLowerCase();
    } else if (typeof id === "number") {
      this.hex = generateHex(id as number);
    } else if (id instanceof ObjectId) {
      this.hex = (id as ObjectId).toHexString();
    } else {
      throw new BSONError("input must be a 24 character hex string, 12 byte Uint8Array, or an integer");
    }
  }

  toHexString(): string {
    return this.hex;
  }
  toString(): string {
    return this.hex;
  }
  toJSON(): string {
    return this.hex;
  }
  /** the same id (another ObjectId, or its 24 hex digits in any case) */
  equals(other: unknown): boolean {
    if (other === null) {
      return false;
    }
    if (other instanceof ObjectId) {
      return (other as ObjectId).toHexString() === this.hex;
    }
    if (typeof other === "string") {
      const s = other as string;
      return isHex24(s) && s.toLowerCase() === this.hex;
    }
    return false;
  }
  /** the time in its first 4 bytes (seconds) */
  getTimestamp(): Date {
    return new Date(parseInt(this.hex.substring(0, 8), 16) * 1000);
  }

  static isValid(id: unknown): boolean {
    return validObjectId(id);
  }
  static createFromHexString(hex: string): ObjectId {
    return new ObjectId(hex);
  }
  /** an id whose timestamp is `seconds` and whose other 8 bytes are zero */
  static createFromTime(seconds: number): ObjectId {
    const t: int = Math.trunc(seconds) % 4294967296;
    const h = hexByte(Math.trunc(t / 16777216) % 256) + hexByte(Math.trunc(t / 65536) % 256) + hexByte(Math.trunc(t / 256) % 256) + hexByte(t % 256);
    return new ObjectId(h + "0000000000000000");
  }
}

function validObjectId(id: unknown): boolean {
  if (id === null) {
    return false;
  }
  if (id instanceof ObjectId) {
    return true;
  }
  if (typeof id === "string") {
    return isHex24(id as string);
  }
  if (typeof id === "number") {
    return Number.isInteger(id as number);
  }
  return false;
}

/** bson's error for an invalid id */
export class BSONError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "BSONError";
  }
}

/** mongoose.isValidObjectId(v): an ObjectId, 24 hex digits, or a number */
export function isValidObjectId(v: unknown): boolean {
  return validObjectId(v);
}

/** mongoose.isObjectIdOrHexString(v): an ObjectId or 24 hex digits */
export function isObjectIdOrHexString(v: unknown): boolean {
  if (v instanceof ObjectId) {
    return true;
  }
  return typeof v === "string" && isHex24(v as string);
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
  private opts: Record<string, unknown> = {};
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

/** updateOne/updateMany's result */
export class UpdateResult {
  acknowledged: boolean = true;
  matchedCount: number = 0;
  modifiedCount: number = 0;
  upsertedCount: number = 0;
  upsertedId: ObjectId | null = null;
}

/** what exists() gives when a document matches */
export interface ExistsResult {
  _id: ObjectId;
}

/** A query: built by a Model (find, findOne, countDocuments...), refined by sort/skip/limit/select/lean, run by
 *  exec() or by awaiting it. R is what it gives. */
export class Query<R, T = unknown> {
  /** the operation: find, findOne, countDocuments, findOneAndUpdate, deleteOne, distinct... */
  readonly op: string;
  readonly model: Model<T>;
  private filter: Record<string, unknown>;
  private update: Record<string, unknown> | null = null;
  private opts: Record<string, unknown> = {};
  private field: string = "";
  private isLean: boolean = false;

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

  /** the command this query sends (what the driver will run) */
  toCommand(): Record<string, unknown> {
    const cmd: Record<string, unknown> = {};
    const coll = this.model.collectionName;
    if (this.op === "find" || this.op === "findOne" || this.op === "findById" || this.op === "exists") {
      cmd["find"] = coll;
      cmd["filter"] = this.filter;
      if (this.opts["sort"] != null) cmd["sort"] = this.opts["sort"];
      if (this.opts["projection"] != null) cmd["projection"] = this.opts["projection"];
      if (this.opts["skip"] != null) cmd["skip"] = this.opts["skip"];
      if (this.op !== "find") cmd["limit"] = 1;
      else if (this.opts["limit"] != null) cmd["limit"] = this.opts["limit"];
    } else if (this.op === "countDocuments") {
      cmd["aggregate"] = coll;
      cmd["pipeline"] = [{ $match: this.filter }, { $group: { _id: 1, n: { $sum: 1 } } }];
    } else if (this.op === "distinct") {
      cmd["distinct"] = coll;
      cmd["key"] = this.field;
      cmd["query"] = this.filter;
    } else if (this.op === "deleteOne" || this.op === "deleteMany") {
      cmd["delete"] = coll;
      cmd["deletes"] = [{ q: this.filter, limit: this.op === "deleteOne" ? 1 : 0 }];
    } else if (this.op === "updateOne" || this.op === "updateMany") {
      cmd["update"] = coll;
      cmd["updates"] = [{ q: this.filter, u: this.update, multi: this.op === "updateMany", upsert: this.opts["upsert"] === true }];
    } else {
      // findOneAndUpdate, findOneAndDelete, findByIdAndDelete...
      cmd["findAndModify"] = coll;
      cmd["query"] = this.filter;
      if (this.update !== null) cmd["update"] = this.update;
      else cmd["remove"] = true;
      if (this.opts["new"] === true) cmd["new"] = true;
      if (this.opts["upsert"] === true) cmd["upsert"] = true;
      if (this.opts["sort"] != null) cmd["sort"] = this.opts["sort"];
    }
    return cmd;
  }

  /** runs the query: without a driver, rejects (NO_DRIVER) */
  async exec(): Promise<R> {
    await this.model.db.runCommand(this.toCommand());
    throw new Error(NO_DRIVER + ": cannot decode the reply of " + this.op);
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

/** A model: the operations on one collection, documents of type T. Every operation needs the driver. */
export class Model<T = unknown> {
  modelName: string = "";
  collectionName: string = "";
  /** the key of its connection (see __registerConnection) */
  connectionKey: string = "";

  /** the connection (it fails when its connection was never made, as a Nest model without MongooseModule.forRoot) */
  get db(): Connection {
    const c = connectionsByKey.get(this.connectionKey);
    if (c === null) {
      throw new MongooseError(NO_DRIVER + ": the connection of model " + this.modelName + " was not opened");
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
    }
    return q;
  }
  findOne(filter: FilterQuery<T> = {}, projection: unknown = null): Query<T | null, T> {
    const q = new Query<T | null, T>(this, "findOne", filter);
    if (projection !== null) q.select(projection);
    return q;
  }
  findById(id: unknown): Query<T | null, T> {
    const f: Record<string, unknown> = {};
    f["_id"] = id;
    return new Query<T | null, T>(this, "findById", f);
  }
  findOneAndUpdate(filter: FilterQuery<T>, update: UpdateQuery<T>, options: QueryOptions<T> | null = null): Query<T | null, T> {
    const q = new Query<T | null, T>(this, "findOneAndUpdate", filter).withUpdate(update);
    if (options !== null) {
      if (options.new != null) q.withOption("new", options.new);
      if (options.upsert != null) q.withOption("upsert", options.upsert);
      if (options.sort != null) q.sort(options.sort);
    }
    return q;
  }
  findByIdAndUpdate(id: unknown, update: UpdateQuery<T>, options: QueryOptions<T> | null = null): Query<T | null, T> {
    const f: Record<string, unknown> = {};
    f["_id"] = id;
    return this.findOneAndUpdate(f, update, options);
  }
  findOneAndDelete(filter: FilterQuery<T>): Query<T | null, T> {
    return new Query<T | null, T>(this, "findOneAndDelete", filter);
  }
  findByIdAndDelete(id: unknown): Query<T | null, T> {
    const f: Record<string, unknown> = {};
    f["_id"] = id;
    return new Query<T | null, T>(this, "findByIdAndDelete", f);
  }
  countDocuments(filter: FilterQuery<T> = {}): Query<number, T> {
    return new Query<number, T>(this, "countDocuments", filter);
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

  /** inserts a document (its fields, or a part of them): without a driver, rejects */
  async create(doc: unknown): Promise<T> {
    await this.db.runCommand(this.insertCommand([doc]));
    throw new Error(NO_DRIVER + ": cannot decode the reply of insert");
  }
  async insertMany(docs: unknown[]): Promise<T[]> {
    await this.db.runCommand(this.insertCommand(docs));
    throw new Error(NO_DRIVER + ": cannot decode the reply of insert");
  }
  /** several writes in one command ({ updateOne: { filter, update, upsert } }...): without a driver, rejects */
  async bulkWrite(ops: unknown[], options: BulkWriteOptions | null = null): Promise<BulkWriteResult> {
    const cmd: Record<string, unknown> = {};
    cmd["bulkWrite"] = this.collectionName;
    cmd["ops"] = ops;
    cmd["ordered"] = options === null || options.ordered !== false;
    await this.db.runCommand(cmd);
    return new BulkWriteResult();
  }
  /** an aggregation pipeline: without a driver, rejects */
  async aggregate(pipeline: unknown[]): Promise<unknown[]> {
    const cmd: Record<string, unknown> = {};
    cmd["aggregate"] = this.collectionName;
    cmd["pipeline"] = pipeline;
    cmd["cursor"] = {};
    await this.db.runCommand(cmd);
    return [];
  }

  private insertCommand(docs: unknown[]): Record<string, unknown> {
    const cmd: Record<string, unknown> = {};
    cmd["insert"] = this.collectionName;
    cmd["documents"] = docs;
    return cmd;
  }
}

/** A hydrated document: T itself (mongoose: T & Document, with _id, save() and toObject()) */
export type HydratedDocument<T> = T;

// ---------- connections ----------

/** mongoose.ConnectionStates / connection.readyState */
export enum ConnectionStates {
  disconnected = 0,
  connected = 1,
  connecting = 2,
  disconnecting = 3,
  uninitialized = 99,
}

/** createConnection / connect options (accepted; the driver reads them in stage 2) */
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

/** The transport stage 2 brings: opens the connection and runs one command (a document) on a database. */
export interface MongoDriver {
  connect(uri: string, options: ConnectOptions): Promise<void>;
  command(dbName: string, command: Record<string, unknown>): Promise<Record<string, unknown>>;
  close(): Promise<void>;
}

let mongoDriver: MongoDriver | null = null;

/** Stage 2: the driver every connection uses (until then operations reject with NO_DRIVER) */
export function setMongoDriver(driver: MongoDriver): void {
  mongoDriver = driver;
}

/** the MongoClient behind a connection (getClient()): its options (hosts, credentials, dbName...) */
export class MongoClient {
  options: unknown = null;
}

/** the database of a connection (connection.db) */
export class Db {
  readonly databaseName: string;
  private readonly conn: Connection;
  constructor(conn: Connection, name: string) {
    this.conn = conn;
    this.databaseName = name;
  }
  /** db.command({ ping: 1 }): without a driver, rejects */
  async command(cmd: Record<string, unknown>): Promise<Record<string, unknown>> {
    return await this.conn.runCommand(cmd);
  }
}

interface Listener {
  event: string;
  once: boolean;
  fn: (arg: unknown) => void;
}

/** A connection (createConnection, or MongooseModule's): models, state, events */
export class Connection {
  /** the database name */
  name: string;
  readonly host: string;
  readyState: ConnectionStates = ConnectionStates.disconnected;
  /** the models made on this connection: their state, by name */
  private modelStates: Record<string, string> = {};
  readonly uri: string;
  readonly options: ConnectOptions;
  private listeners: Listener[] = [];
  private client: MongoClient = new MongoClient();
  private dbs: Record<string, Connection> = {};
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

  /** connection.db: the database (null until connected) */
  get db(): Db | null {
    return this.readyState === ConnectionStates.connected ? new Db(this, this.name) : null;
  }

  getClient(): MongoClient {
    return this.client;
  }

  /** opens the connection: without a driver, rejects (and readyState goes back to disconnected) */
  async openUri(uri: string = "", options: ConnectOptions | null = null): Promise<Connection> {
    const target = uri.length > 0 ? uri : this.uri;
    this.readyState = ConnectionStates.connecting;
    const d = mongoDriver;
    if (d === null) {
      this.readyState = ConnectionStates.disconnected;
      const err = new MongooseError(NO_DRIVER + ": cannot connect to " + redactUri(target));
      this.emit("error", err);
      throw err;
    }
    await d.connect(target, options ?? this.options);
    this.readyState = ConnectionStates.connected;
    this.emit("connected", null);
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
    const d = mongoDriver;
    if (d !== null && this.readyState === ConnectionStates.connected) {
      this.readyState = ConnectionStates.disconnecting;
      await d.close();
    }
    const was = this.readyState;
    this.readyState = ConnectionStates.disconnected;
    if (was !== ConnectionStates.disconnected) {
      this.emit("disconnected", null);
    }
    this.emit("close", null);
  }

  /** runs one command on this connection's database: the stage-2 seam. Without a driver, rejects. */
  async runCommand(command: Record<string, unknown>): Promise<Record<string, unknown>> {
    const d = mongoDriver;
    const first = Object.keys(command);
    const what = first.length > 0 ? first[0] + (typeof command[first[0]] === "string" ? " on " + (command[first[0]] as string) : "") : "a command";
    if (d === null) {
      throw new MongooseError(NO_DRIVER + ": cannot run " + what);
    }
    if (this.readyState !== ConnectionStates.connected) {
      throw new MongooseError("Connection " + this.name + " is not open: cannot run " + what);
    }
    return await d.command(this.name, command);
  }

  /** connection.model(name, schema, collection?): the model (made once per name) */
  model<T = unknown>(name: string, schema: Schema | null = null, collection: string = ""): Model<T> {
    const have = this.modelStates[name];
    if (have != null && schema === null) {
      const m: Model<T> = JSON.parse(have);
      return m;
    }
    if (schema === null) {
      throw new MongooseError("Schema hasn't been registered for model \"" + name + "\".\nUse mongoose.model(name, schema)");
    }
    const state = __modelState(this.key, name, schema, collection);
    this.modelStates[name] = state;
    const m: Model<T> = JSON.parse(state);
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
    c.readyState = this.readyState;
    c.client = this.client;
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

/** mongoose's base error */
export class MongooseError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "MongooseError";
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

/** the URI without its password (for messages) */
function redactUri(uri: string): string {
  const start = uri.indexOf("://");
  const at = uri.indexOf("@");
  if (start < 0 || at < 0) {
    return uri;
  }
  const cred = uri.substring(start + 3, at);
  const colon = cred.indexOf(":");
  return colon < 0 ? uri : uri.substring(0, start + 3) + cred.substring(0, colon) + ":****" + uri.substring(at);
}

/** mongoose.createConnection(uri, options): the connection, opening (asPromise() waits; without a driver it rejects) */
export function createConnection(uri: string = "", options: ConnectOptions = {}): Connection {
  const c = new Connection(uri, options);
  if (uri.length > 0) {
    c.readyState = ConnectionStates.connecting;
  }
  return c;
}
