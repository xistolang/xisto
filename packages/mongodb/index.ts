// mongodb (the Node driver, v6) for xisto: the part mongoose needs, over native/mongo.c (OP_MSG, BSON, a pool per
// worker thread, SCRAM-SHA-256). Also bson's ObjectId, which mongoose re-exports.
//
// MongoClient(url, options): mongodb://[user:pass@]host[:port][,host...]/[db][?options] with authSource,
// authMechanism (SCRAM-SHA-256 or DEFAULT), appName, replicaSet, directConnection, serverSelectionTimeoutMS,
// connectTimeoutMS, socketTimeoutMS, maxPoolSize. connect() selects the server (the primary of a replica set, found
// through hello) and fails after serverSelectionTimeoutMS with MongoServerSelectionError carrying the last error
// ("connect ECONNREFUSED 127.0.0.1:27017"). db(name).command(cmd), db.collection(name) with find/findOne/insertOne/
// insertMany/updateOne/updateMany/deleteOne/deleteMany/countDocuments/aggregate/drop.
//
// Values written: string, number (an integer in the int32 range as int32, else double, as js-bson), boolean, null,
// Date, ObjectId, Buffer/Uint8Array (binary), bigint (int64), arrays, Records and objects of classes. Read back:
// ObjectId as its hex string (a field typed ObjectId reads it into an ObjectId), a date as ISO text (a Date field
// reads it), int64 as a number, decimal128 as { $numberDecimal }, binary as Node's Buffer JSON.
// Not yet: TLS, mongodb+srv:// (DNS SRV), SCRAM-SHA-1 and other mechanisms, sessions/transactions, a RegExp value
// (write { $regex, $options }), change streams, server monitoring (a server that goes away is seen at the next
// command).

declare class NativeClient {
  constructor(config: object);
  connect(): string;
  command(db: string, text: string): string;
  cursor(db: string, text: string): string;
  close(): void;
  encodeHex(text: string): string;
  decodeHex(hex: string): string;
}

/** a document: a Record (what replies and raw reads give) */
export type Document = Record<string, unknown>;

// ---------- bson: ObjectId ----------

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

/** an ObjectId, 24 hex digits, or an integer (bson's ObjectId.isValid) */
export function validObjectId(id: unknown): boolean {
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

/** bson's error for an invalid value */
export class BSONError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "BSONError";
  }
}

// ---------- values -> command text (read by native/mongo.c) ----------

/** a value as the text native/mongo.c turns into BSON: JSON with $O"hex", $D<ms>, $B"base64", $L<n> tags */
export function toBsonText(v: unknown): string {
  if (v === null) {
    return "null";
  }
  if (typeof v === "string") {
    return JSON.stringify(v as string);
  }
  if (typeof v === "number") {
    const n = v as number;
    if (Number.isNaN(n)) return "NaN";
    if (!Number.isFinite(n)) return n > 0 ? "Infinity" : "-Infinity";
    return String(n);
  }
  if (typeof v === "boolean") {
    return (v as boolean) ? "true" : "false";
  }
  if (typeof v === "bigint") {
    return "$L" + String(v as bigint);
  }
  if (v instanceof ObjectId) {
    return '$O"' + (v as ObjectId).toHexString() + '"';
  }
  if (v instanceof Date) {
    return "$D" + String((v as Date).getTime());
  }
  if (v instanceof Uint8Array) {
    return '$B"' + (v as Uint8Array).toBase64() + '"';
  }
  if (Array.isArray(v)) {
    const xs = v as unknown[];
    let out = "[";
    for (let i = 0; i < xs.length; i++) {
      if (i > 0) out += ",";
      const x = xs[i];
      out += typeof x === "function" ? "null" : toBsonText(x);
    }
    return out + "]";
  }
  const rec = v as Record<string, unknown>;
  // an optional field of a class that was never set reads as null here; JSON.stringify knows it is absent
  // (undefined in Node, which BSON leaves out): only the keys it writes are kept, when some value is null
  let present: Record<string, unknown> | null = null;
  for (const x of Object.values(rec)) {
    if (x === null) {
      present = JSON.parse(JSON.stringify(v));
      break;
    }
  }
  let out = "{";
  let first = true;
  for (const [k, x] of Object.entries(rec)) {
    if (typeof x === "function") continue;
    if (x === null && present !== null && !(k in present)) continue;
    if (!first) out += ",";
    first = false;
    out += JSON.stringify(k) + ":" + toBsonText(x);
  }
  return out + "}";
}

let codec: NativeClient | null = null;

function codecClient(): NativeClient {
  const c = codec;
  if (c !== null) {
    return c;
  }
  const none: Record<string, unknown>[] = [];
  const n = new NativeClient({ hosts: none });
  codec = n;
  return n;
}

/** the BSON bytes of a document, as lowercase hex (bson's serialize, for tests and tools) */
export function serializeHex(doc: unknown): string {
  return codecClient().encodeHex(toBsonText(doc));
}

/** a BSON document (hex) as JSON text: ObjectId as hex, dates as ISO text */
export function deserializeHexToJson(hex: string): string {
  return codecClient().decodeHex(hex);
}

// ---------- errors ----------

/** the driver's base error */
export class MongoError extends Error {
  /** the server's error code (0 when the error is the driver's) */
  code: int = 0;
  constructor(message: string) {
    super(message);
    this.name = "MongoError";
  }
}

/** a network error (a connection closed or timed out) */
export class MongoNetworkError extends MongoError {
  constructor(message: string) {
    super(message);
    this.name = "MongoNetworkError";
  }
}

/** no server could be selected within serverSelectionTimeoutMS: the message is the last error */
export class MongoServerSelectionError extends MongoError {
  constructor(message: string) {
    super(message);
    this.name = "MongoServerSelectionError";
  }
}

/** a connection string the driver cannot read */
export class MongoParseError extends MongoError {
  constructor(message: string) {
    super(message);
    this.name = "MongoParseError";
  }
}

/** an option or feature the xisto driver does not have yet */
export class MongoAPIError extends MongoError {
  constructor(message: string) {
    super(message);
    this.name = "MongoAPIError";
  }
}

/** an error the server answered with: code, codeName, errmsg; a duplicate key also has keyPattern and keyValue */
export class MongoServerError extends MongoError {
  codeName: string = "";
  errmsg: string = "";
  /** the reply (or the write error) the server sent */
  errorResponse: Document = {};
  keyPattern: Document | null = null;
  keyValue: Document | null = null;
  /** the index of the operation, for a write error */
  index: int = 0;
  constructor(message: string) {
    super(message);
    this.name = "MongoServerError";
  }
}

/** insertMany's error when a document could not be written (the first write error's code, message and keys) */
export class MongoBulkWriteError extends MongoServerError {
  constructor(message: string) {
    super(message);
    this.name = "MongoBulkWriteError";
  }
}

interface ErrorReply {
  ok?: number;
  errmsg?: string;
  code?: int;
  codeName?: string;
}

/** one write error of an insert/update/delete reply */
export interface WriteError {
  index: int;
  code: int;
  errmsg: string;
  keyPattern?: Document;
  keyValue?: Document;
}

/** a MongoServerError from a reply whose ok is not 1 */
export function serverError(replyText: string): MongoServerError {
  const r: ErrorReply = JSON.parse(replyText);
  const e = new MongoServerError(r.errmsg ?? "command failed");
  e.code = r.code ?? 0;
  e.codeName = r.codeName ?? "";
  e.errmsg = r.errmsg ?? "";
  e.errorResponse = JSON.parse(replyText);
  return e;
}

/** a MongoServerError from a write error (what insertOne/updateOne/deleteOne throw) */
export function writeErrorToError(w: WriteError): MongoServerError {
  const e = new MongoServerError(w.errmsg);
  e.code = w.code;
  e.errmsg = w.errmsg;
  e.index = w.index;
  if (w.keyPattern != null) e.keyPattern = w.keyPattern;
  if (w.keyValue != null) e.keyValue = w.keyValue;
  const resp: Document = {};
  resp["index"] = w.index;
  resp["code"] = w.code;
  resp["errmsg"] = w.errmsg;
  if (w.keyPattern != null) resp["keyPattern"] = w.keyPattern;
  if (w.keyValue != null) resp["keyValue"] = w.keyValue;
  e.errorResponse = resp;
  return e;
}

// the native layer's errors: "Network|msg", "ServerSelection|msg", "Server|code|codeName|msg"
function driverError(e: Error): Error {
  const m = e.message;
  const bar = m.indexOf("|");
  if (bar < 0) {
    return e;
  }
  const kind = m.substring(0, bar);
  const rest = m.substring(bar + 1);
  if (kind === "Network") return new MongoNetworkError(rest);
  if (kind === "ServerSelection") return new MongoServerSelectionError(rest);
  if (kind === "Server") {
    const parts = rest.split("|");
    const se = new MongoServerError(parts.slice(2).join("|"));
    se.code = Math.trunc(parseInt(parts[0], 10));
    se.codeName = parts[1];
    se.errmsg = se.message;
    return se;
  }
  return e;
}

// ---------- connection strings ----------

/** a host of the connection string (options.hosts): toString() is "host:port" */
export class HostAddress {
  host: string;
  port: int;
  constructor(host: string, port: int) {
    this.host = host;
    this.port = port;
  }
  toString(): string {
    return this.host + ":" + String(this.port);
  }
}

/** the credentials of a connection string (options.credentials) */
export class MongoCredentials {
  username: string = "";
  password: string = "";
  /** the database the user is defined in (authSource; else the URI's database; else admin) */
  source: string = "admin";
  mechanism: string = "DEFAULT";
}

/** what new MongoClient(url, options) accepts */
export interface MongoClientOptions {
  dbName?: string;
  user?: string;
  pass?: string;
  auth?: { username?: string; password?: string };
  authSource?: string;
  authMechanism?: string;
  appName?: string;
  replicaSet?: string;
  directConnection?: boolean;
  serverSelectionTimeoutMS?: number;
  connectTimeoutMS?: number;
  socketTimeoutMS?: number;
  maxPoolSize?: number;
  minPoolSize?: number;
  tls?: boolean;
  ssl?: boolean;
  monitorCommands?: boolean;
  family?: number;
}

/** the options in effect (client.options), with the URI read: hosts, credentials, dbName, timeouts... */
export class MongoOptions {
  hosts: HostAddress[] = [];
  credentials: MongoCredentials | null = null;
  /** the database in the URI's path (test when there is none) */
  dbName: string = "test";
  srvHost: string | null = null;
  replicaSet: string | null = null;
  directConnection: boolean = false;
  tls: boolean = false;
  appName: string | null = null;
  serverSelectionTimeoutMS: number = 30000;
  connectTimeoutMS: number = 30000;
  socketTimeoutMS: number = 0;
  maxPoolSize: number = 100;
}

function decodePart(s: string): string {
  try {
    return decodeURIComponent(s);
  } catch (e) {
    throw new MongoParseError("URI malformed");
  }
}

function numberOption(name: string, v: string): number {
  const n = Number(v);
  if (Number.isNaN(n)) {
    throw new MongoParseError("Expected " + name + " to be stringified int value, got: " + v);
  }
  return n;
}

/** reads a connection string (and the options given next to it, which win) */
export function parseOptions(url: string, options: MongoClientOptions = {}): MongoOptions {
  const o = new MongoOptions();
  let rest = "";
  if (url.startsWith("mongodb://")) {
    rest = url.substring(10);
  } else if (url.startsWith("mongodb+srv://")) {
    throw new MongoAPIError("mongodb+srv:// connection strings are not supported by xisto's driver yet (use mongodb://host1,host2/?replicaSet=...)");
  } else {
    throw new MongoParseError('Invalid scheme, expected connection string to start with "mongodb://" or "mongodb+srv://"');
  }
  let query = "";
  const q = rest.indexOf("?");
  if (q >= 0) {
    query = rest.substring(q + 1);
    rest = rest.substring(0, q);
  }
  let path = "";
  const slash = rest.indexOf("/");
  if (slash >= 0) {
    path = rest.substring(slash + 1);
    rest = rest.substring(0, slash);
  }
  const at = rest.lastIndexOf("@");
  let user = "";
  let pass = "";
  let hasUser = false;
  if (at >= 0) {
    const cred = rest.substring(0, at);
    rest = rest.substring(at + 1);
    const colon = cred.indexOf(":");
    user = decodePart(colon >= 0 ? cred.substring(0, colon) : cred);
    pass = colon >= 0 ? decodePart(cred.substring(colon + 1)) : "";
    hasUser = true;
  }
  if (rest.length === 0) {
    throw new MongoParseError("Invalid connection string, no hosts");
  }
  for (const h of rest.split(",")) {
    let host = h;
    let port: int = 27017;
    if (h.startsWith("[")) { /* [::1]:27017 */
      const close = h.indexOf("]");
      host = h.substring(1, close);
      if (h.length > close + 2) port = Math.trunc(parseInt(h.substring(close + 2), 10));
    } else {
      const c = h.lastIndexOf(":");
      if (c >= 0) {
        host = h.substring(0, c);
        port = Math.trunc(parseInt(h.substring(c + 1), 10));
      }
    }
    if (Number.isNaN(port)) {
      throw new MongoParseError("Invalid port in host " + h);
    }
    o.hosts.push(new HostAddress(host.toLowerCase(), port));
  }
  if (path.length > 0) {
    o.dbName = decodePart(path);
  }
  let authSource = "";
  let mechanism = "";
  if (query.length > 0) {
    for (const kv of query.split("&")) {
      if (kv.length === 0) continue;
      const eq = kv.indexOf("=");
      const k = eq >= 0 ? kv.substring(0, eq) : kv;
      const v = eq >= 0 ? decodePart(kv.substring(eq + 1)) : "";
      const key = k.toLowerCase();
      if (key === "authsource") authSource = v;
      else if (key === "authmechanism") mechanism = v;
      else if (key === "appname") o.appName = v;
      else if (key === "replicaset") o.replicaSet = v;
      else if (key === "directconnection") o.directConnection = v === "true";
      else if (key === "serverselectiontimeoutms") o.serverSelectionTimeoutMS = numberOption(k, v);
      else if (key === "connecttimeoutms") o.connectTimeoutMS = numberOption(k, v);
      else if (key === "sockettimeoutms") o.socketTimeoutMS = numberOption(k, v);
      else if (key === "maxpoolsize") o.maxPoolSize = numberOption(k, v);
      else if (key === "tls" || key === "ssl") o.tls = v === "true";
    }
  }
  // options next to the URI win
  if (options.appName != null) o.appName = options.appName;
  if (options.replicaSet != null) o.replicaSet = options.replicaSet;
  if (options.directConnection != null) o.directConnection = options.directConnection;
  if (options.serverSelectionTimeoutMS != null) o.serverSelectionTimeoutMS = options.serverSelectionTimeoutMS;
  if (options.connectTimeoutMS != null) o.connectTimeoutMS = options.connectTimeoutMS;
  if (options.socketTimeoutMS != null) o.socketTimeoutMS = options.socketTimeoutMS;
  if (options.maxPoolSize != null) o.maxPoolSize = options.maxPoolSize;
  if (options.tls === true || options.ssl === true) o.tls = true;
  if (options.authSource != null) authSource = options.authSource;
  if (options.authMechanism != null) mechanism = options.authMechanism;
  const au = options.auth;
  if (options.user != null) { user = options.user; hasUser = true; }
  if (options.pass != null) pass = options.pass;
  if (au != null && au.username != null) { user = au.username; hasUser = true; }
  if (au != null && au.password != null) pass = au.password;
  if (hasUser) {
    const cr = new MongoCredentials();
    cr.username = user;
    cr.password = pass;
    cr.source = authSource.length > 0 ? authSource : path.length > 0 ? o.dbName : "admin";
    cr.mechanism = mechanism.length > 0 ? mechanism : "DEFAULT";
    o.credentials = cr;
  }
  return o;
}

// ---------- the client ----------

/** MongoClient: connects (server selection), runs commands on databases */
export class MongoClient {
  readonly options: MongoOptions;
  private native: NativeClient | null = null;
  private connected: boolean = false;

  constructor(url: string, options: MongoClientOptions = {}) {
    this.options = parseOptions(url, options);
  }

  private client(): NativeClient {
    const have = this.native;
    if (have !== null) {
      return have;
    }
    const o = this.options;
    if (o.tls) {
      throw new MongoAPIError("TLS connections are not supported by xisto's driver yet");
    }
    const cr = o.credentials;
    if (cr !== null && cr.mechanism !== "DEFAULT" && cr.mechanism !== "SCRAM-SHA-256") {
      throw new MongoAPIError("authMechanism " + cr.mechanism + " is not supported by xisto's driver yet (SCRAM-SHA-256 is)");
    }
    const hosts: Record<string, unknown>[] = [];
    for (const h of o.hosts) {
      hosts.push({ host: h.host, port: h.port });
    }
    const cfg: Record<string, unknown> = {
      hosts: hosts,
      connectTimeoutMS: o.connectTimeoutMS,
      serverSelectionTimeoutMS: o.serverSelectionTimeoutMS,
      socketTimeoutMS: o.socketTimeoutMS,
      maxPoolSize: o.maxPoolSize,
      directConnection: o.directConnection,
    };
    if (o.appName !== null) cfg["appName"] = o.appName;
    if (o.replicaSet !== null) cfg["replicaSet"] = o.replicaSet;
    if (cr !== null) {
      cfg["user"] = cr.username;
      cfg["password"] = cr.password;
      cfg["source"] = cr.source;
    }
    const n = new NativeClient(cfg);
    this.native = n;
    return n;
  }

  /** selects the server and opens the first connection (MongoServerSelectionError after serverSelectionTimeoutMS) */
  async connect(): Promise<MongoClient> {
    const n = this.client();
    try {
      n.connect();
    } catch (e) {
      throw driverError(e as Error);
    }
    this.connected = true;
    return this;
  }

  /** a database (the URI's when no name is given) */
  db(name: string = ""): Db {
    return new Db(this, name.length > 0 ? name : this.options.dbName);
  }

  /** closes the idle connections */
  async close(force: boolean = false): Promise<void> {
    const n = this.native;
    if (n !== null) {
      n.close();
    }
    this.connected = false;
  }

  /** @internal a command on a database: the reply as JSON text (MongoServerError when ok is not 1) */
  commandText(db: string, cmd: Document): string {
    const n = this.client();
    let reply = "";
    try {
      reply = n.command(db, toBsonText(cmd));
    } catch (e) {
      throw driverError(e as Error);
    }
    checkOk(reply);
    return reply;
  }

  /** @internal a command whose reply is a cursor (find, aggregate), drained: the documents as JSON text ([...]) */
  cursorText(db: string, cmd: Document): string {
    const n = this.client();
    let reply = "";
    try {
      reply = n.cursor(db, toBsonText(cmd));
    } catch (e) {
      throw driverError(e as Error);
    }
    checkOk(reply);
    // {"ok":1,"docs":[...]}: the array
    return reply.substring(15, reply.length - 1);
  }
}

interface OkReply {
  ok?: number;
}

function checkOk(reply: string): void {
  const r: OkReply = JSON.parse(reply);
  if (r.ok !== 1) {
    throw serverError(reply);
  }
}

/** a database of a client */
export class Db {
  readonly client: MongoClient;
  readonly databaseName: string;
  constructor(client: MongoClient, name: string) {
    this.client = client;
    this.databaseName = name;
  }
  /** db.command({ ping: 1 }): the reply, as JSON data (`as Reply` reads it into an interface; as a Record: it
   *  is Record<string, unknown> too) */
  async command(cmd: Document): Promise<object> {
    const r: object = JSON.parse(this.client.commandText(this.databaseName, cmd));
    return r;
  }
  collection(name: string): Collection {
    return new Collection(this, name);
  }
  async dropDatabase(): Promise<boolean> {
    this.client.commandText(this.databaseName, { dropDatabase: 1 });
    return true;
  }
}

/** insertOne's result */
export class InsertOneResult {
  acknowledged: boolean = true;
  insertedId: unknown = null;
}

/** insertMany's result */
export class InsertManyResult {
  acknowledged: boolean = true;
  insertedCount: number = 0;
}

/** updateOne/updateMany's result */
export class UpdateResult {
  acknowledged: boolean = true;
  matchedCount: number = 0;
  modifiedCount: number = 0;
  upsertedCount: number = 0;
  upsertedId: ObjectId | null = null;
}

/** deleteOne/deleteMany's result */
export class DeleteResult {
  acknowledged: boolean = true;
  deletedCount: number = 0;
}

/** an insert/update/delete reply */
export interface WriteReply {
  ok?: number;
  n?: number;
  nModified?: number;
  upserted?: UpsertedEntry[];
  writeErrors?: WriteError[];
}

export interface UpsertedEntry {
  index: int;
  _id: ObjectId;
}

/** the write reply of a command, MongoServerError for its first write error */
export function writeReply(text: string): WriteReply {
  const r: WriteReply = JSON.parse(text);
  const we = r.writeErrors;
  if (we != null && we.length > 0) {
    throw writeErrorToError(we[0]);
  }
  return r;
}

/** a find cursor: toArray() runs it */
export class FindCursor {
  private readonly coll: Collection;
  private readonly cmd: Document;
  constructor(coll: Collection, cmd: Document) {
    this.coll = coll;
    this.cmd = cmd;
  }
  sort(s: Document): FindCursor {
    this.cmd["sort"] = s;
    return this;
  }
  skip(n: number): FindCursor {
    this.cmd["skip"] = n;
    return this;
  }
  limit(n: number): FindCursor {
    this.cmd["limit"] = n;
    return this;
  }
  project(p: Document): FindCursor {
    this.cmd["projection"] = p;
    return this;
  }
  async toArray(): Promise<Document[]> {
    const docs: Document[] = JSON.parse(this.coll.db.client.cursorText(this.coll.db.databaseName, this.cmd));
    return docs;
  }
}

/** a collection of a database */
export class Collection {
  readonly db: Db;
  readonly collectionName: string;
  constructor(db: Db, name: string) {
    this.db = db;
    this.collectionName = name;
  }
  get dbName(): string {
    return this.db.databaseName;
  }
  private run(cmd: Document): string {
    return this.db.client.commandText(this.db.databaseName, cmd);
  }
  find(filter: Document = {}): FindCursor {
    return new FindCursor(this, { find: this.collectionName, filter: filter });
  }
  async findOne(filter: Document = {}): Promise<Document | null> {
    const docs: Document[] = JSON.parse(this.db.client.cursorText(this.db.databaseName, { find: this.collectionName, filter: filter, limit: 1, singleBatch: true }));
    return docs.length > 0 ? docs[0] : null;
  }
  async insertOne(doc: Document): Promise<InsertOneResult> {
    if (!("_id" in doc)) {
      const withId: Document = { _id: new ObjectId() };
      for (const [k, v] of Object.entries(doc)) withId[k] = v;
      doc = withId;
    }
    writeReply(this.run({ insert: this.collectionName, documents: [doc], ordered: true }));
    const r = new InsertOneResult();
    r.insertedId = doc["_id"];
    return r;
  }
  async insertMany(docs: Document[]): Promise<InsertManyResult> {
    const all: Document[] = [];
    for (const d of docs) {
      if ("_id" in d) {
        all.push(d);
      } else {
        const withId: Document = { _id: new ObjectId() };
        for (const [k, v] of Object.entries(d)) withId[k] = v;
        all.push(withId);
      }
    }
    const w = writeReply(this.run({ insert: this.collectionName, documents: all, ordered: true }));
    const r = new InsertManyResult();
    r.insertedCount = w.n ?? 0;
    return r;
  }
  private async update(filter: Document, update: Document, multi: boolean, upsert: boolean): Promise<UpdateResult> {
    const u: Document = { q: filter, u: update };
    if (upsert) u["upsert"] = true;
    if (multi) u["multi"] = true;
    const w = writeReply(this.run({ update: this.collectionName, updates: [u], ordered: true }));
    const r = new UpdateResult();
    const ups = w.upserted ?? [];
    r.upsertedCount = ups.length;
    r.matchedCount = (w.n ?? 0) - ups.length;
    r.modifiedCount = w.nModified ?? 0;
    if (ups.length > 0) r.upsertedId = ups[0]._id;
    return r;
  }
  async updateOne(filter: Document, update: Document, options: { upsert?: boolean } = {}): Promise<UpdateResult> {
    return await this.update(filter, update, false, options.upsert === true);
  }
  async updateMany(filter: Document, update: Document, options: { upsert?: boolean } = {}): Promise<UpdateResult> {
    return await this.update(filter, update, true, options.upsert === true);
  }
  private async remove(filter: Document, limit: int): Promise<DeleteResult> {
    const w = writeReply(this.run({ delete: this.collectionName, deletes: [{ q: filter, limit: limit }], ordered: true }));
    const r = new DeleteResult();
    r.deletedCount = w.n ?? 0;
    return r;
  }
  async deleteOne(filter: Document = {}): Promise<DeleteResult> {
    return await this.remove(filter, 1);
  }
  async deleteMany(filter: Document = {}): Promise<DeleteResult> {
    return await this.remove(filter, 0);
  }
  async countDocuments(filter: Document = {}): Promise<number> {
    const docs: CountDoc[] = JSON.parse(this.db.client.cursorText(this.db.databaseName, {
      aggregate: this.collectionName,
      pipeline: [{ $match: filter }, { $group: { _id: 1, n: { $sum: 1 } } }],
      cursor: {},
    }));
    return docs.length > 0 ? docs[0].n : 0;
  }
  async aggregate(pipeline: Document[]): Promise<Document[]> {
    const docs: Document[] = JSON.parse(this.db.client.cursorText(this.db.databaseName, { aggregate: this.collectionName, pipeline: pipeline, cursor: {} }));
    return docs;
  }
  /** drops the collection (false when it did not exist) */
  async drop(): Promise<boolean> {
    try {
      this.run({ drop: this.collectionName });
      return true;
    } catch (e) {
      if (e instanceof MongoServerError && e.code === 26) {
        return false;
      }
      throw e;
    }
  }
}

interface CountDoc {
  n: number;
}
