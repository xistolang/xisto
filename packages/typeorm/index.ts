// typeorm for xisto: entities and views from decorators, a DataSource over a driver, repositories, a query builder,
// raw queries and transactions; the subset a Nest service uses, with TypeORM's names and SQL.
//
// Results are `object` (JSON text) that a typed declaration decodes, as with pg:
//   const rows: Row[] = await dataSource.query("SELECT ...", [id]);
// Entities come back as the class itself: `const users: User[] = await repo.find({ where: { active: true } });`
//
// Drivers: "postgres" through the pg package; "oracle" through a driver registered with registerDriver("oracle", ...)
// (the oracledb package does it), so the SQL here is written for both dialects: $1 or :1 binds, LIMIT/OFFSET or
// OFFSET/FETCH, quoted identifiers as TypeORM quotes them.
import { ClassInfo, FieldInfo } from "xisto:reflect";
import { Pool } from "pg";
import { Connection, OUT_FORMAT_OBJECT, OracleError, createPool } from "oracledb";

declare function remap(rows: object, spec: string): object;
declare function get(json: object, key: string): object;
declare function at(json: object, i: int): object;
declare function len(json: object): int;
declare function kind(json: object): string;
declare function keys(json: object): object;

// ---------------------------------------------------------------------------------------------------------------
// metadata

export interface EntityOptions {
  name?: string;
  schema?: string;
  synchronize?: boolean;
}

export interface ViewEntityOptions {
  name?: string;
  schema?: string;
  expression?: string;
  synchronize?: boolean;
}

export interface ColumnOptions {
  name?: string;
  type?: string;
  nullable?: boolean;
  length?: int;
  precision?: int;
  scale?: int;
  default?: string | number | boolean;
  primary?: boolean;
  select?: boolean;
  unique?: boolean;
  comment?: string;
}

export interface ViewColumnOptions {
  name?: string;
}

/** One mapped property: its column, how its value reads, whether it is part of the primary key. */
export class ColumnMetadata {
  propertyName: string = "";
  databaseName: string = "";
  kind: string = "other";
  isPrimary: boolean = false;
  isGenerated: boolean = false;
  isNullable: boolean = false;
  isSelect: boolean = true;
  type: string = "";
}

/** An entity or view: its table, its columns, the ClassInfo of its class. */
export class EntityMetadata {
  id: string = "";
  name: string = "";
  tableName: string = "";
  schema: string = "";
  isView: boolean = false;
  expression: string = "";
  columns: ColumnMetadata[] = [];

  primaryColumns(): ColumnMetadata[] {
    const out: ColumnMetadata[] = [];
    for (const c of this.columns) {
      if (c.isPrimary) {
        out.push(c);
      }
    }
    return out;
  }

  findColumn(propertyName: string): ColumnMetadata | null {
    for (const c of this.columns) {
      if (c.propertyName === propertyName) {
        return c;
      }
    }
    return null;
  }

  /** "column\tproperty\tkind" per column: what the native remap reads */
  spec(): string {
    let s = "";
    for (const c of this.columns) {
      s = s + c.databaseName + "\t" + c.propertyName + "\t" + columnKind(c) + "\n";
    }
    return s;
  }
}

const entityMetadata: Map<string, EntityMetadata> = new Map<string, EntityMetadata>();

function metadataOf(c: ClassInfo): EntityMetadata {
  const have = entityMetadata.get(c.id);
  if (have !== null) {
    return have;
  }
  const m = new EntityMetadata();
  m.id = c.id;
  m.name = c.name;
  m.tableName = c.name;
  entityMetadata.set(c.id, m);
  return m;
}

/** The metadata the decorators recorded for an entity class (an error when it has none). */
export function getMetadata(entity: ClassInfo): EntityMetadata {
  const m = entityMetadata.get(entity.id);
  if (m === null) {
    throw new EntityMetadataNotFoundError(entity.name);
  }
  return m;
}

function metadataById(id: string, name: string): EntityMetadata {
  const m = entityMetadata.get(id);
  if (m === null) {
    throw new EntityMetadataNotFoundError(name);
  }
  return m;
}

// TypeORM's error classes: a failed query throws a QueryFailedError with the driver's message and error
export class EntityMetadataNotFoundError extends Error {
  constructor(entity: string) {
    super(`No metadata for "${entity}" was found.`);
    this.name = "EntityMetadataNotFoundError";
  }
}

export class QueryFailedError extends Error {
  readonly query: string;
  readonly parameters: unknown[];
  /** the driver's own error ("ORA-00942: ...", "relation ... does not exist") */
  readonly driverError: Error;
  /** the driver error's fields, as TypeORM copies them: Oracle's errorNum (20001 for ORA-20001; 0 otherwise),
      code ("ORA-20001") and offset */
  errorNum: int = 0;
  code: string = "";
  offset: int = 0;
  constructor(query: string, parameters: unknown[], driverError: Error) {
    super(driverError.message);
    this.name = "QueryFailedError";
    this.query = query;
    this.parameters = parameters;
    this.driverError = driverError;
    if (driverError instanceof OracleError) {
      const o = driverError as OracleError;
      this.errorNum = o.errorNum;
      this.code = o.code;
      this.offset = o.offset;
    }
  }
}

function kindOf(f: FieldInfo): string {
  const k = f.type.kind;
  if (k === "string" || k === "int" || k === "number" || k === "boolean") {
    return k;
  }
  if (f.type.id === "builtin#Date") {
    return "date";
  }
  const id = f.type.id;
  if (id.startsWith("string")) {
    return "string";
  }
  if (id.startsWith("int")) {
    return "int";
  }
  if (id.startsWith("number")) {
    return "number";
  }
  if (id.startsWith("boolean")) {
    return "boolean";
  }
  if (id.indexOf("Date") >= 0) {
    return "date";
  }
  return "other";
}

/** a column's kind for binds and rows: a `type: "date"` column (a day, no time) gets ":day", as TypeORM writes it
    with TO_DATE('YYYY-MM-DD') and reads back the local calendar day */
function columnKind(c: ColumnMetadata): string {
  return c.type === "date" ? c.kind + ":day" : c.kind;
}

function addColumn(c: ClassInfo, f: FieldInfo, o: ColumnOptions, primary: boolean, generated: boolean): void {
  const m = metadataOf(c);
  const col = new ColumnMetadata();
  col.propertyName = f.name;
  col.databaseName = o.name ?? f.name;
  col.kind = kindOf(f);
  col.isPrimary = primary || (o.primary ?? false);
  col.isGenerated = generated;
  col.isNullable = o.nullable ?? false;
  col.isSelect = o.select ?? true;
  col.type = o.type ?? "";
  m.columns.push(col); // (field decorators run in declaration order)
}

/** @Entity("TABLE") or @Entity({ name, schema }): the class maps to a table. */
export function Entity(nameOrOptions: string | EntityOptions = ""): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    const m = metadataOf(c);
    if (typeof nameOrOptions === "string") {
      if (nameOrOptions.length > 0) {
        m.tableName = nameOrOptions;
      }
    } else {
      m.tableName = nameOrOptions.name ?? c.name;
      m.schema = nameOrOptions.schema ?? "";
    }
  };
}

/** @ViewEntity("VIEW") or @ViewEntity({ name, schema, expression }): the class maps to a view (read only). */
export function ViewEntity(nameOrOptions: string | ViewEntityOptions = ""): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    const m = metadataOf(c);
    m.isView = true;
    if (typeof nameOrOptions === "string") {
      if (nameOrOptions.length > 0) {
        m.tableName = nameOrOptions;
      }
    } else {
      m.tableName = nameOrOptions.name ?? c.name;
      m.schema = nameOrOptions.schema ?? "";
      m.expression = nameOrOptions.expression ?? "";
    }
  };
}

/** @Column() or @Column({ name, type, nullable, ... }): the property maps to a column. */
export function Column(options: ColumnOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    addColumn(c, f, options, false, false);
  };
}

/** @PrimaryColumn({ name, type }): a column of the primary key. */
export function PrimaryColumn(options: ColumnOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    addColumn(c, f, options, true, false);
  };
}

/** @PrimaryGeneratedColumn(): a primary key the database generates (left out of INSERT). */
export function PrimaryGeneratedColumn(strategy: string = "increment"): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    const o: ColumnOptions = {};
    addColumn(c, f, o, true, true);
  };
}

/** @ViewColumn() or @ViewColumn({ name }): a column of a view entity. */
export function ViewColumn(options: ViewColumnOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {
    const o: ColumnOptions = { name: options.name ?? f.name };
    addColumn(c, f, o, false, false);
  };
}

// ---------------------------------------------------------------------------------------------------------------
// find operators: values of a where, written as JSON markers the SQL builder turns into SQL

function operator(name: string, value: object): object {
  const text = '{"__op":' + JSON.stringify(name) + ',"value":' + JSON.stringify(value) + "}";
  const o: object = JSON.parse(text);
  return o;
}
function operatorNoValue(name: string): object {
  const o: object = JSON.parse('{"__op":' + JSON.stringify(name) + "}");
  return o;
}

export function Equal(value: object): object {
  return operator("=", value);
}
export function Not(value: object): object {
  return operator("<>", value);
}
export function LessThan(value: object): object {
  return operator("<", value);
}
export function LessThanOrEqual(value: object): object {
  return operator("<=", value);
}
export function MoreThan(value: object): object {
  return operator(">", value);
}
export function MoreThanOrEqual(value: object): object {
  return operator(">=", value);
}
export function Like(value: string): object {
  return operator("like", value);
}
export function ILike(value: string): object {
  return operator("ilike", value);
}
export function In(values: object): object {
  return operator("in", values);
}
export function Between(from: object, to: object): object {
  const o: object = JSON.parse('{"__op":"between","value":' + JSON.stringify(from) + ',"to":' + JSON.stringify(to) + "}");
  return o;
}
export function IsNull(): object {
  return operatorNoValue("isnull");
}

// ---------------------------------------------------------------------------------------------------------------
// drivers

/** What a query returns from a driver: {"rows":[{column: value}], "rowCount": n} */
export interface DriverResult {
  rows: object;
  rowCount: int;
}

/** A pinned connection: the statements of a transaction run on it. */
export abstract class DriverSession {
  /** the driver's own connection (an oracledb Connection for "oracle"; null when the driver has none to give) */
  raw: unknown = null;
  abstract run(sql: string, params: unknown[]): Promise<object>;
  abstract begin(): Promise<void>;
  abstract commit(): Promise<void>;
  abstract rollback(): Promise<void>;
  abstract release(): Promise<void>;
}

/** A database: its SQL dialect, a pool of connections. */
export abstract class Driver {
  abstract dialect(): string;
  abstract connect(): Promise<void>;
  abstract close(): Promise<void>;
  abstract run(sql: string, params: unknown[]): Promise<object>;
  abstract session(): Promise<DriverSession>;
}

export interface DataSourceOptions {
  type: string;
  host?: string;
  port?: int;
  username?: string;
  password?: string;
  database?: string;
  schema?: string;
  connectString?: string;
  sid?: string;
  serviceName?: string;
  entities?: ClassInfo[];
  synchronize?: boolean;
  logging?: boolean;
  poolSize?: int;
  name?: string;
}

/** Builds the driver for one `type` of DataSourceOptions ("oracle" comes from the oracledb package). */
export interface DriverFactory {
  create: (options: DataSourceOptions) => Driver;
}

const driverFactories: Map<string, DriverFactory> = new Map<string, DriverFactory>();

/** registerDriver("oracle", { create: (o) => new OracleDriver(...) }): what DataSource uses for that type. */
export function registerDriver(type: string, factory: DriverFactory): void {
  driverFactories.set(type, factory);
}

// postgres: the pg package's pool; a transaction runs on a pool of one connection, one transaction at a time
// (pg's Pool has no client checkout yet)
class PostgresSession extends DriverSession {
  private driver: PostgresDriver;
  constructor(driver: PostgresDriver) {
    super();
    this.driver = driver;
  }
  async run(sql: string, params: unknown[]): Promise<object> {
    return this.driver.runTx(sql, params);
  }
  async begin(): Promise<void> {
    await this.driver.runTx("BEGIN", []);
  }
  async commit(): Promise<void> {
    await this.driver.runTx("COMMIT", []);
  }
  async rollback(): Promise<void> {
    await this.driver.runTx("ROLLBACK", []);
  }
  async release(): Promise<void> {
    this.driver.txBusy = false;
  }
}

export class PostgresDriver extends Driver {
  private options: DataSourceOptions;
  private pool: Pool | null = null;
  private txPool: Pool | null = null;
  txBusy: boolean = false;

  constructor(options: DataSourceOptions) {
    super();
    this.options = options;
  }
  dialect(): string {
    return "postgres";
  }
  private makePool(max: int): Pool {
    return new Pool({
      host: this.options.host ?? "localhost",
      port: this.options.port ?? 5432,
      user: this.options.username ?? "postgres",
      password: this.options.password ?? "",
      database: this.options.database ?? "postgres",
      max: max,
    });
  }
  async connect(): Promise<void> {
    this.pool = this.makePool(this.options.poolSize ?? 10);
  }
  async close(): Promise<void> {
    this.pool = null;
    this.txPool = null;
  }
  async run(sql: string, params: unknown[]): Promise<object> {
    const p = this.pool;
    if (p === null) {
      throw new Error("the DataSource is not initialized: call initialize() first");
    }
    return p.query(sql, params);
  }
  async runTx(sql: string, params: unknown[]): Promise<object> {
    const have = this.txPool;
    if (have !== null) {
      return have.query(sql, params);
    }
    const made = this.makePool(1);
    this.txPool = made;
    return made.query(sql, params);
  }
  async session(): Promise<DriverSession> {
    if (this.txBusy) {
      throw new Error("postgres: one transaction at a time (the pg package has no client checkout yet)");
    }
    this.txBusy = true;
    return new PostgresSession(this);
  }
}

// oracle: any pool with node-oracledb's shape, given as functions (the oracledb package builds them)
export interface OracleConnectionFunctions {
  execute: (sql: string, binds: unknown[]) => Promise<object>; // {"rows":[{COLUMN: value}],"rowsAffected":n,"outBinds":...}
  commit: () => Promise<void>;
  rollback: () => Promise<void>;
  close: () => Promise<void>;
  /** the driver's connection itself (QueryRunner.connect() returns it, as TypeORM does), or null */
  raw?: unknown;
}
export interface OraclePoolFunctions {
  getConnection: () => Promise<OracleConnectionFunctions>;
  close: () => Promise<void>;
}

function oracleResult(r: object): object {
  const rows = get(r, "rows");
  const affected = get(r, "rowsAffected");
  const outBinds = get(r, "outBinds");
  const n = kind(affected) === "number" ? jsonInt(affected) : kind(rows) === "array" ? len(rows) : 0;
  // a PL/SQL block's OUT binds go with the result: query() returns them, as TypeORM returns raw.outBinds
  const out = kind(outBinds) === "null" ? "" : ',"outBinds":' + JSON.stringify(outBinds);
  const ra = kind(affected) === "number" ? ',"rowsAffected":' + String(n) : "";
  const text = '{"rows":' + (kind(rows) === "array" ? JSON.stringify(rows) : "[]") + ',"rowCount":' + String(n) + ra + out + "}";
  const o: object = JSON.parse(text);
  return o;
}

class OracleSession extends DriverSession {
  private conn: OracleConnectionFunctions;
  constructor(conn: OracleConnectionFunctions, raw: unknown) {
    super();
    this.conn = conn;
    this.raw = raw;
  }
  async run(sql: string, params: unknown[]): Promise<object> {
    return oracleResult(await this.conn.execute(sql, params));
  }
  async begin(): Promise<void> {
    // Oracle starts a transaction with the first statement; autocommit is off on a pinned connection
  }
  async commit(): Promise<void> {
    await this.conn.commit();
  }
  async rollback(): Promise<void> {
    await this.conn.rollback();
  }
  async release(): Promise<void> {
    await this.conn.close();
  }
}

export class OracleDriver extends Driver {
  private makePool: () => Promise<OraclePoolFunctions>;
  private pool: OraclePoolFunctions | null = null;
  constructor(makePool: () => Promise<OraclePoolFunctions>) {
    super();
    this.makePool = makePool;
  }
  dialect(): string {
    return "oracle";
  }
  async connect(): Promise<void> {
    this.pool = await this.makePool();
  }
  async close(): Promise<void> {
    const p = this.pool;
    if (p !== null) {
      await p.close();
    }
    this.pool = null;
  }
  async run(sql: string, params: unknown[]): Promise<object> {
    const p = this.pool;
    if (p === null) {
      throw new Error("the DataSource is not initialized: call initialize() first");
    }
    const conn = await p.getConnection();
    try {
      const r = await conn.execute(sql, params);
      await conn.commit();
      return oracleResult(r);
    } finally {
      await conn.close();
    }
  }
  async session(): Promise<DriverSession> {
    const p = this.pool;
    if (p === null) {
      throw new Error("the DataSource is not initialized: call initialize() first");
    }
    const c = await p.getConnection();
    return new OracleSession(c, c.raw ?? null);
  }
}

// oracle through the oracledb package, as TypeORM loads node-oracledb for type "oracle" (no registerDriver needed):
// connectString from options.connectString, or host:port/serviceName (database or sid as the service otherwise)
function oracleConnection(c: Connection): OracleConnectionFunctions {
  return {
    execute: async (sql: string, binds: unknown[]): Promise<object> => c.execute(sql, binds, { outFormat: OUT_FORMAT_OBJECT }),
    commit: async (): Promise<void> => {
      await c.commit();
    },
    rollback: async (): Promise<void> => {
      await c.rollback();
    },
    close: async (): Promise<void> => {
      await c.close();
    },
    raw: c,
  };
}

function oracleConnectString(o: DataSourceOptions): string {
  const given = o.connectString ?? "";
  if (given.length > 0) {
    return given;
  }
  const service = o.serviceName ?? o.database ?? o.sid ?? "";
  return (o.host ?? "localhost") + ":" + String(o.port ?? 1521) + (service.length > 0 ? "/" + service : "");
}

async function oraclePool(o: DataSourceOptions): Promise<OraclePoolFunctions> {
  const p = await createPool({
    user: o.username ?? "",
    password: o.password ?? "",
    connectString: oracleConnectString(o),
    poolMax: o.poolSize ?? 10,
  });
  return {
    getConnection: async (): Promise<OracleConnectionFunctions> => oracleConnection(await p.getConnection()),
    close: async (): Promise<void> => {
      await p.close();
    },
  };
}

// ---------------------------------------------------------------------------------------------------------------
// SQL helpers

function jsonInt(v: object): int {
  const t = JSON.stringify(v);
  const n: number = JSON.parse(t);
  return Math.trunc(n);
}

/** A JSON value as a driver parameter: string, number, boolean or null; arrays and objects as their JSON text. */
function toParam(v: object): unknown {
  const k = kind(v);
  const t = JSON.stringify(v);
  if (k === "string") {
    const s: string = JSON.parse(t);
    return s;
  }
  if (k === "number") {
    const n: number = JSON.parse(t);
    if (Number.isInteger(n)) {
      return Math.trunc(n);
    }
    return n;
  }
  if (k === "boolean") {
    const b: boolean = JSON.parse(t);
    return b;
  }
  if (k === "null") {
    return null;
  }
  return t;
}

/** The parameters of one statement, numbered in the dialect's style. */
class Binds {
  dialect: string;
  values: unknown[] = [];
  constructor(dialect: string) {
    this.dialect = dialect;
  }
  add(v: unknown): string {
    this.values.push(v);
    const n = this.values.length;
    return this.dialect === "oracle" ? ":" + String(n) : "$" + String(n);
  }
  addJson(v: object): string {
    return this.add(toParam(v));
  }
  /** a value of a column: on Oracle a Date column's value (an ISO string once it went through JSON) binds as a Date,
      so it reaches the database as DATE/TIMESTAMP, as node-oracledb binds a JS Date for TypeORM */
  addColumnJson(v: object, columnKind: string): string {
    if (this.dialect === "oracle" && columnKind.endsWith(":day") && kind(v) === "string") {
      // the local calendar day, as TypeORM's TO_DATE('YYYY-MM-DD', 'YYYY-MM-DD') (a bind here, not a literal)
      const text: string = JSON.parse(JSON.stringify(v));
      let dayText = text;
      if (text.length > 10) {
        const d = new Date(text);
        if (!Number.isNaN(d.getTime())) {
          dayText = String(d.getFullYear()).padStart(4, "0") + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0");
        }
      }
      return "TO_DATE(" + this.add(dayText.slice(0, 10)) + ", 'YYYY-MM-DD')";
    }
    if (this.dialect === "oracle" && columnKind === "date" && kind(v) === "string") {
      const iso: string = JSON.parse(JSON.stringify(v));
      const d = new Date(iso);
      if (!Number.isNaN(d.getTime())) {
        return this.add(d);
      }
    }
    return this.addJson(v);
  }
}

function quote(name: string): string {
  return '"' + name.split('"').join('""') + '"';
}

function tablePath(m: EntityMetadata): string {
  if (m.schema.length > 0) {
    return quote(m.schema) + "." + quote(m.tableName);
  }
  return quote(m.tableName);
}

function columnName(m: EntityMetadata, property: string): string {
  const c = m.findColumn(property);
  return c === null ? property : c.databaseName;
}

function isOperator(v: object): boolean {
  return kind(v) === "object" && kind(get(v, "__op")) === "string";
}

function opName(v: object): string {
  const s: string = JSON.parse(JSON.stringify(get(v, "__op")));
  return s;
}

/** The SQL of one property condition: "col" = $1, "col" IS NULL, "col" IN ($1, $2), ... */
function conditionSql(column: string, value: object, binds: Binds, columnKind: string = ""): string {
  const k = kind(value);
  if (k === "null") {
    return column + " IS NULL";
  }
  if (!isOperator(value)) {
    return column + " = " + binds.addColumnJson(value, columnKind);
  }
  const op = opName(value);
  const v = get(value, "value");
  if (op === "isnull") {
    return column + " IS NULL";
  }
  if (op === "<>" && isOperator(v) && opName(v) === "isnull") {
    return column + " IS NOT NULL";
  }
  if (op === "<>" && isOperator(v)) {
    return "NOT(" + conditionSql(column, v, binds, columnKind) + ")";
  }
  if (op === "in") {
    const parts: string[] = [];
    const n = len(v);
    for (let i = 0; i < n; i++) {
      parts.push(binds.addColumnJson(at(v, i), columnKind));
    }
    if (parts.length === 0) {
      return "0=1";
    }
    return column + " IN (" + parts.join(", ") + ")";
  }
  if (op === "between") {
    return column + " BETWEEN " + binds.addColumnJson(v, columnKind) + " AND " + binds.addColumnJson(get(value, "to"), columnKind);
  }
  if (op === "like") {
    return column + " LIKE " + binds.addJson(v);
  }
  if (op === "ilike") {
    if (binds.dialect === "postgres") {
      return column + " ILIKE " + binds.addJson(v);
    }
    return "UPPER(" + column + ") LIKE UPPER(" + binds.addJson(v) + ")";
  }
  return column + " " + op + " " + binds.addColumnJson(v, columnKind);
}

/** where: { prop: value, ... } (AND) or [{...}, {...}] (OR of ANDs), properties as columns of alias */
function whereSql(m: EntityMetadata, where: object, alias: string, binds: Binds): string {
  const k = kind(where);
  if (k === "array") {
    const ors: string[] = [];
    const n = len(where);
    for (let i = 0; i < n; i++) {
      const one = whereSql(m, at(where, i), alias, binds);
      if (one.length > 0) {
        ors.push("(" + one + ")");
      }
    }
    return ors.join(" OR ");
  }
  if (k !== "object") {
    return "";
  }
  const props: string[] = JSON.parse(JSON.stringify(keys(where)));
  const ands: string[] = [];
  for (const p of props) {
    const col = (alias.length > 0 ? quote(alias) + "." : "") + quote(columnName(m, p));
    const meta = m.findColumn(p);
    ands.push(conditionSql(col, get(where, p), binds, meta === null ? "" : columnKind(meta)));
  }
  return ands.join(" AND ");
}

function orderSql(m: EntityMetadata, order: object, alias: string): string {
  if (kind(order) !== "object") {
    return "";
  }
  const props: string[] = JSON.parse(JSON.stringify(keys(order)));
  const parts: string[] = [];
  for (const p of props) {
    const dir: string = JSON.parse(JSON.stringify(get(order, p)));
    const d = dir.toUpperCase() === "DESC" ? "DESC" : "ASC";
    parts.push((alias.length > 0 ? quote(alias) + "." : "") + quote(columnName(m, p)) + " " + d);
  }
  return parts.join(", ");
}

/** LIMIT/OFFSET (postgres) or OFFSET ... ROWS FETCH NEXT ... ROWS ONLY (oracle 12c+) */
function pageSql(dialect: string, skip: int, take: int): string {
  if (skip <= 0 && take <= 0) {
    return "";
  }
  if (dialect === "oracle") {
    let s = " OFFSET " + String(skip > 0 ? skip : 0) + " ROWS";
    if (take > 0) {
      s = s + " FETCH NEXT " + String(take) + " ROWS ONLY";
    }
    return s;
  }
  let s = "";
  if (take > 0) {
    s = s + " LIMIT " + String(take);
  }
  if (skip > 0) {
    s = s + " OFFSET " + String(skip);
  }
  return s;
}

function selectList(m: EntityMetadata, alias: string, only: string[]): string {
  const parts: string[] = [];
  for (const c of m.columns) {
    if (only.length > 0 && only.indexOf(c.propertyName) < 0) {
      continue;
    }
    if (only.length === 0 && !c.isSelect) {
      continue;
    }
    parts.push(quote(alias) + "." + quote(c.databaseName) + " AS " + quote(c.databaseName));
  }
  return parts.join(", ");
}

// ---------------------------------------------------------------------------------------------------------------
// find options and results

/** TypeORM's where / partial entity types: an object of the entity's columns (read as JSON by the query builder) */
export type FindOptionsWhere<T> = object;
export type DeepPartial<T> = object;
export type ObjectLiteral = object;

export interface FindManyOptions {
  where?: object;
  order?: object;
  skip?: int;
  take?: int;
  select?: string[];
}

export interface FindOneOptions {
  where?: object;
  order?: object;
  select?: string[];
}

export interface UpdateResult {
  affected: int;
  raw: object;
}

export interface DeleteResult {
  affected: int;
  raw: object;
}

export interface InsertResult {
  raw: object;
  identifiers: object;
}

/**
 * Wraps every statement a DataSource runs (repositories, query builders, query runners, dataSource.query):
 * what Node code does by patching QueryRunner.prototype.query. `next()` runs the statement (or the next wrapper).
 */
export type QueryWrapper = (sql: string, params: unknown[], next: () => Promise<object>) => Promise<object>;

// the wrappers from the last added (outermost) to the first, then the statement itself
function runWrapped(ws: QueryWrapper[], i: int, sql: string, params: unknown[], last: () => Promise<object>): Promise<object> {
  if (i < 0) {
    return last();
  }
  return ws[i](sql, params, (): Promise<object> => runWrapped(ws, i - 1, sql, params, last));
}

/** Statements reach the database through a DataSource (its pool) or a QueryRunner (its pinned connection). */
export class Executor {
  dataSource: DataSource;
  session: DriverSession | null;
  sessionId: int = -1;
  constructor(dataSource: DataSource, session: DriverSession | null, sessionId: int = -1) {
    this.dataSource = dataSource;
    this.session = session;
    this.sessionId = sessionId;
  }
  dialect(): string {
    return this.dataSource.driver().dialect();
  }
  async run(sql: string, params: unknown[]): Promise<object> {
    if (!this.dataSource.isInitialized) {
      await this.dataSource.initialize(); // a DataSource from TypeOrmModule connects on its first query
    }
    if (this.dataSource.options.logging ?? false) {
      console.log("query: " + sql);
    }
    const s = this.session;
    const ds = this.dataSource;
    const direct = async (): Promise<object> => {
      try {
        if (s !== null) {
          return await s.run(sql, params);
        }
        return await ds.driver().run(sql, params);
      } catch (e) {
        throw new QueryFailedError(sql, params, e);
      }
    };
    const ws = ds.queryWrappers;
    if (ws.length === 0) {
      return await direct();
    }
    return await runWrapped(ws, ws.length - 1, sql, params, direct);
  }
  async rows(sql: string, params: unknown[]): Promise<object> {
    const r = await this.run(sql, params);
    const out = get(r, "outBinds");
    return kind(out) === "null" ? get(r, "rows") : out; // (TypeORM: raw.rows || raw.outBinds)
  }
}

// the DataSources by name and the open sessions (QueryRunners) by id: what a Repository finds its executor through
const dataSources: Map<string, DataSource> = new Map<string, DataSource>();
const sessions: Map<int, DriverSession> = new Map<int, DriverSession>();
let nextSession: int = 0;

/** registers a DataSource under its name ("default" when options.name is not given) */
export function registerDataSource(ds: DataSource): void {
  dataSources.set(ds.options.name ?? "default", ds);
}

function executorFor(dataSourceName: string, sessionId: int): Executor {
  const ds = dataSources.get(dataSourceName);
  if (ds === null) {
    throw new Error(`No DataSource named "${dataSourceName}": create it (new DataSource(...)) or import TypeOrmModule.forRoot`);
  }
  return new Executor(ds, sessionId >= 0 ? sessions.get(sessionId) : null, sessionId);
}

/**
 * Repository<Entity>: find, save, update, delete and query builders for one entity class.
 * Its state is two plain values (the DataSource's name, the session it runs on), so a dependency injector can make
 * the exact Repository<User> a constructor asks for from its parameter's type (TypeOrmModule.forFeature).
 */
export class Repository<T> {
  dataSourceName: string = "default";
  sessionId: int = -1;

  constructor(dataSourceName: string = "default", sessionId: int = -1) {
    this.dataSourceName = dataSourceName;
    this.sessionId = sessionId;
  }

  get metadata(): EntityMetadata {
    return metadataById(__reflect_typeid(T), __reflect_typeid(T));
  }

  get manager(): EntityManager {
    return new EntityManager(executorFor(this.dataSourceName, this.sessionId));
  }

  private get executor(): Executor {
    return executorFor(this.dataSourceName, this.sessionId);
  }

  private alias(): string {
    return this.metadata.name;
  }

  private entities(rows: object): T[] {
    const mapped = remap(rows, this.metadata.spec());
    const out: T[] = JSON.parse(JSON.stringify(mapped));
    return out;
  }

  private selectSql(where: object | null, order: object | null, skip: int, take: int, select: string[], binds: Binds): string {
    const a = this.alias();
    let sql = "SELECT " + selectList(this.metadata, a, select) + " FROM " + tablePath(this.metadata) + " " + quote(a);
    if (where !== null) {
      const w = whereSql(this.metadata, where, a, binds);
      if (w.length > 0) {
        sql = sql + " WHERE " + w;
      }
    }
    if (order !== null) {
      const o = orderSql(this.metadata, order, a);
      if (o.length > 0) {
        sql = sql + " ORDER BY " + o;
      }
    }
    return sql + pageSql(binds.dialect, skip, take);
  }

  /** The SELECT a find runs (with its parameters), for tests and logging. */
  findSql(options: FindManyOptions = {}): string {
    const binds = new Binds(this.executor.dialect());
    return this.selectSql(options.where ?? null, options.order ?? null, options.skip ?? 0, options.take ?? 0, options.select ?? [], binds);
  }

  async find(options: FindManyOptions = {}): Promise<T[]> {
    const binds = new Binds(this.executor.dialect());
    const sql = this.selectSql(options.where ?? null, options.order ?? null, options.skip ?? 0, options.take ?? 0, options.select ?? [], binds);
    return this.entities(await this.executor.rows(sql, binds.values));
  }

  async findBy(where: object): Promise<T[]> {
    return this.find({ where: where });
  }

  async findOne(options: FindOneOptions): Promise<T | null> {
    const binds = new Binds(this.executor.dialect());
    const sql = this.selectSql(options.where ?? null, options.order ?? null, 0, 1, options.select ?? [], binds);
    const found = this.entities(await this.executor.rows(sql, binds.values));
    return found.length > 0 ? found[0] : null;
  }

  async findOneBy(where: object): Promise<T | null> {
    return this.findOne({ where: where });
  }

  async findOneOrFail(options: FindOneOptions): Promise<T> {
    const one = await this.findOne(options);
    if (one === null) {
      throw new Error(`Could not find any entity of type "${this.metadata.name}" matching the criteria`);
    }
    return one;
  }

  async count(options: FindManyOptions = {}): Promise<int> {
    const binds = new Binds(this.executor.dialect());
    const a = this.alias();
    let sql = 'SELECT COUNT(*) AS "cnt" FROM ' + tablePath(this.metadata) + " " + quote(a);
    const where = options.where ?? null;
    if (where !== null) {
      const w = whereSql(this.metadata, where, a, binds);
      if (w.length > 0) {
        sql = sql + " WHERE " + w;
      }
    }
    const rows = await this.executor.rows(sql, binds.values);
    return countOf(rows);
  }

  async countBy(where: object): Promise<int> {
    return this.count({ where: where });
  }

  async exists(options: FindManyOptions = {}): Promise<boolean> {
    return (await this.count(options)) > 0;
  }

  /** a new entity from plain values (DeepPartial): the properties the object gives */
  create(values: object): T {
    const e: T = JSON.parse(JSON.stringify(values));
    return e;
  }

  private writable(values: object, withPrimary: boolean, binds: Binds): string[][] {
    const cols: string[] = [];
    const vals: string[] = [];
    const props: string[] = JSON.parse(JSON.stringify(keys(values)));
    for (const p of props) {
      const c = this.metadata.findColumn(p);
      if (c === null || (c.isGenerated && !withPrimary) || (!withPrimary && c.isPrimary)) {
        continue;
      }
      cols.push(quote(c.databaseName));
      vals.push(binds.addColumnJson(get(values, p), columnKind(c)));
    }
    return [cols, vals];
  }

  private primaryWhere(values: object): object {
    let text = "{";
    let first = true;
    for (const c of this.metadata.primaryColumns()) {
      if (!first) {
        text = text + ",";
      }
      first = false;
      text = text + JSON.stringify(c.propertyName) + ":" + JSON.stringify(get(values, c.propertyName));
    }
    const w: object = JSON.parse(text + "}");
    return w;
  }

  async insert(entity: object): Promise<InsertResult> {
    if (this.metadata.isView) {
      throw new Error(`${this.metadata.name} is a view entity: it cannot be inserted`);
    }
    const binds = new Binds(this.executor.dialect());
    const cv = this.writable(entity, true, binds);
    const sql = "INSERT INTO " + tablePath(this.metadata) + " (" + cv[0].join(", ") + ") VALUES (" + cv[1].join(", ") + ")";
    const r = await this.executor.run(sql, binds.values);
    return { raw: r, identifiers: this.primaryWhere(entity) };
  }

  /** INSERT when no row has the entity's primary key, UPDATE of its other columns when one has */
  async save(entity: T): Promise<T> {
    const values: object = JSON.parse(JSON.stringify(entity));
    const pks = this.metadata.primaryColumns();
    let present = pks.length > 0;
    for (const c of pks) {
      if (kind(get(values, c.propertyName)) === "null") {
        present = false;
      }
    }
    if (present && (await this.count({ where: this.primaryWhere(values) })) > 0) {
      await this.update(this.primaryWhere(values), values);
      return entity;
    }
    const binds = new Binds(this.executor.dialect());
    const cv = this.writable(values, true, binds);
    let sql = "INSERT INTO " + tablePath(this.metadata) + " (" + cv[0].join(", ") + ") VALUES (" + cv[1].join(", ") + ")";
    let generated = false;
    for (const c of pks) {
      if (c.isGenerated) {
        generated = true;
      }
    }
    if (generated && this.executor.dialect() === "postgres") {
      sql = sql + " RETURNING *";
      const rows = await this.executor.rows(sql, binds.values);
      const made = this.entities(rows);
      return made.length > 0 ? made[0] : entity;
    }
    await this.executor.run(sql, binds.values);
    return entity;
  }

  async update(criteria: object, partial: object): Promise<UpdateResult> {
    if (this.metadata.isView) {
      throw new Error(`${this.metadata.name} is a view entity: it cannot be updated`);
    }
    const binds = new Binds(this.executor.dialect());
    const cv = this.writable(partial, false, binds);
    const sets: string[] = [];
    for (let i = 0; i < cv[0].length; i++) {
      sets.push(cv[0][i] + " = " + cv[1][i]);
    }
    const w = whereSql(this.metadata, criteria, "", binds);
    const sql = "UPDATE " + tablePath(this.metadata) + " SET " + sets.join(", ") + (w.length > 0 ? " WHERE " + w : "");
    const r = await this.executor.run(sql, binds.values);
    return { affected: jsonInt(get(r, "rowCount")), raw: r };
  }

  async delete(criteria: object): Promise<DeleteResult> {
    if (this.metadata.isView) {
      throw new Error(`${this.metadata.name} is a view entity: it cannot be deleted`);
    }
    const binds = new Binds(this.executor.dialect());
    const w = whereSql(this.metadata, criteria, "", binds);
    const sql = "DELETE FROM " + tablePath(this.metadata) + (w.length > 0 ? " WHERE " + w : "");
    const r = await this.executor.run(sql, binds.values);
    return { affected: jsonInt(get(r, "rowCount")), raw: r };
  }

  async remove(entity: T): Promise<T> {
    const values: object = JSON.parse(JSON.stringify(entity));
    await this.delete(this.primaryWhere(values));
    return entity;
  }

  async query<T = any>(sql: string, params: unknown[] = []): Promise<T> {
    const raw: object = await this.executor.rows(sql, params);
    const rows: T = raw; // query<Row[]>(...) decodes the rows as Row[], like TypeORM's query<T>
    return rows;
  }

  createQueryBuilder(alias: string = ""): SelectQueryBuilder<T> {
    const a = alias.length > 0 ? alias : this.metadata.name;
    const qb = new SelectQueryBuilder<T>(this.executor);
    qb.from(this.metadata.tableName, a);
    qb.setMain(this.metadata, a);
    return qb;
  }
}

function countOf(rows: object): int {
  if (len(rows) === 0) {
    return 0;
  }
  const row = at(rows, 0);
  let v = get(row, "cnt");
  if (kind(v) === "null") {
    v = get(row, "CNT");
  }
  if (kind(v) === "string") {
    const s: string = JSON.parse(JSON.stringify(v));
    return Math.trunc(Number(s));
  }
  return jsonInt(v);
}

// ---------------------------------------------------------------------------------------------------------------
// query builder: one class for SELECT, UPDATE, INSERT and DELETE (TypeORM's names: createQueryBuilder(...).select,
// .update().set().where().execute(), .insert().into().values().execute(), .delete().from().where().execute())

interface Join {
  kind: string;
  target: string;
  alias: string;
  condition: string;
}

export class SelectQueryBuilder<T> {
  private executor: Executor;
  private mode: string = "select";
  private mainMeta: EntityMetadata | null = null;
  private mainAlias: string = "";
  private table: string = "";
  private selects: string[] = [];
  private wheres: string[] = [];
  private joins: Join[] = [];
  private orders: string[] = [];
  private groups: string[] = [];
  private skipN: int = 0;
  private takeN: int = 0;
  private params: string = "{}";
  private setValues: string = "{}";
  private insertValues: string = "[]";
  private aliases: Map<string, EntityMetadata> = new Map<string, EntityMetadata>();

  constructor(executor: Executor) {
    this.executor = executor;
  }

  /** (internal) the entity the builder selects */
  setMain(m: EntityMetadata, alias: string): SelectQueryBuilder<T> {
    this.mainMeta = m;
    this.mainAlias = alias;
    this.aliases.set(alias, m);
    return this;
  }

  private mergeParams(more: object): void {
    if (kind(more) !== "object") {
      return;
    }
    const names: string[] = JSON.parse(JSON.stringify(keys(more)));
    const cur: object = JSON.parse(this.params);
    const curNames: string[] = JSON.parse(JSON.stringify(keys(cur)));
    let text = "{";
    let first = true;
    for (const n of curNames) {
      if (names.indexOf(n) >= 0) {
        continue;
      }
      text = text + (first ? "" : ",") + JSON.stringify(n) + ":" + JSON.stringify(get(cur, n));
      first = false;
    }
    for (const n of names) {
      text = text + (first ? "" : ",") + JSON.stringify(n) + ":" + JSON.stringify(get(more, n));
      first = false;
    }
    this.params = text + "}";
  }

  select(selection: string | string[] = []): SelectQueryBuilder<T> {
    this.selects = [];
    if (typeof selection === "string") {
      this.selects.push(selection);
    } else {
      for (const s of selection) {
        this.selects.push(s);
      }
    }
    return this;
  }

  addSelect(selection: string, alias: string = ""): SelectQueryBuilder<T> {
    this.selects.push(alias.length > 0 ? selection + " AS " + quote(alias) : selection);
    return this;
  }

  /** from(Entity, "alias") or from("TABLE", "alias") */
  from(target: string | ClassInfo, alias: string): SelectQueryBuilder<T> {
    if (typeof target === "string") {
      this.table = target.indexOf(".") >= 0 || target.indexOf("(") >= 0 ? target : quote(target);
    } else {
      const m = getMetadata(target);
      this.table = tablePath(m);
      this.setMain(m, alias);
    }
    this.mainAlias = alias;
    return this;
  }

  where(condition: string, params: object = {}): SelectQueryBuilder<T> {
    this.wheres = ["(" + condition + ")"];
    this.mergeParams(params);
    return this;
  }

  andWhere(condition: string, params: object = {}): SelectQueryBuilder<T> {
    this.wheres.push((this.wheres.length > 0 ? "AND " : "") + "(" + condition + ")");
    this.mergeParams(params);
    return this;
  }

  orWhere(condition: string, params: object = {}): SelectQueryBuilder<T> {
    this.wheres.push((this.wheres.length > 0 ? "OR " : "") + "(" + condition + ")");
    this.mergeParams(params);
    return this;
  }

  setParameter(name: string, value: object): SelectQueryBuilder<T> {
    const o: object = JSON.parse("{" + JSON.stringify(name) + ":" + JSON.stringify(value) + "}");
    this.mergeParams(o);
    return this;
  }

  setParameters(params: object): SelectQueryBuilder<T> {
    this.mergeParams(params);
    return this;
  }

  private join(kindName: string, target: string | ClassInfo, alias: string, condition: string, params: object): SelectQueryBuilder<T> {
    let t = "";
    if (typeof target === "string") {
      t = target.indexOf(".") >= 0 || target.indexOf("(") >= 0 ? target : quote(target);
    } else {
      const m = getMetadata(target);
      t = tablePath(m);
      this.aliases.set(alias, m);
    }
    this.joins.push({ kind: kindName, target: t, alias: alias, condition: condition });
    this.mergeParams(params);
    return this;
  }

  innerJoin(target: string | ClassInfo, alias: string, condition: string = "", params: object = {}): SelectQueryBuilder<T> {
    return this.join("INNER JOIN", target, alias, condition, params);
  }

  leftJoin(target: string | ClassInfo, alias: string, condition: string = "", params: object = {}): SelectQueryBuilder<T> {
    return this.join("LEFT JOIN", target, alias, condition, params);
  }

  orderBy(sort: string, order: string = "ASC"): SelectQueryBuilder<T> {
    this.orders = [sort + " " + (order.toUpperCase() === "DESC" ? "DESC" : "ASC")];
    return this;
  }

  addOrderBy(sort: string, order: string = "ASC"): SelectQueryBuilder<T> {
    this.orders.push(sort + " " + (order.toUpperCase() === "DESC" ? "DESC" : "ASC"));
    return this;
  }

  groupBy(group: string): SelectQueryBuilder<T> {
    this.groups = [group];
    return this;
  }

  addGroupBy(group: string): SelectQueryBuilder<T> {
    this.groups.push(group);
    return this;
  }

  skip(count: number): SelectQueryBuilder<T> { // (a number, as TypeORM takes: its integer part)
    const n: int = Math.trunc(count);
    this.skipN = n;
    return this;
  }
  take(count: number): SelectQueryBuilder<T> { // (a number, as TypeORM takes: its integer part)
    const n: int = Math.trunc(count);
    this.takeN = n;
    return this;
  }
  offset(count: number): SelectQueryBuilder<T> { // (a number, as TypeORM takes: its integer part)
    const n: int = Math.trunc(count);
    this.skipN = n;
    return this;
  }
  limit(count: number): SelectQueryBuilder<T> { // (a number, as TypeORM takes: its integer part)
    const n: int = Math.trunc(count);
    this.takeN = n;
    return this;
  }

  /** update(Entity).set({ prop: value }).where(...).execute() */
  update(target: ClassInfo | null = null): SelectQueryBuilder<T> {
    this.mode = "update";
    if (target !== null) {
      const m = getMetadata(target);
      this.table = tablePath(m);
      this.mainMeta = m;
      this.aliases.set(m.name, m);
    }
    return this;
  }

  set(values: object): SelectQueryBuilder<T> {
    this.setValues = JSON.stringify(values);
    return this;
  }

  /** insert().into(Entity).values({...} | [{...}]).execute() */
  insert(): SelectQueryBuilder<T> {
    this.mode = "insert";
    return this;
  }

  into(target: string | ClassInfo, columns: string[] = []): SelectQueryBuilder<T> {
    if (typeof target === "string") {
      this.table = quote(target);
    } else {
      const m = getMetadata(target);
      this.table = tablePath(m);
      this.mainMeta = m;
    }
    return this;
  }

  values(values: object): SelectQueryBuilder<T> {
    this.insertValues = kind(values) === "array" ? JSON.stringify(values) : "[" + JSON.stringify(values) + "]";
    return this;
  }

  /** delete().from(Entity).where(...).execute() */
  delete(): SelectQueryBuilder<T> {
    this.mode = "delete";
    return this;
  }

  // alias.property (outside quotes) -> "alias"."COLUMN" for the aliases whose entity is known, as TypeORM does
  private columnsIn(sql: string): string {
    let out = "";
    let i = 0;
    const n = sql.length;
    while (i < n) {
      const ch = sql.charAt(i);
      if (ch === "'" || ch === '"') {
        let j = i + 1;
        while (j < n && sql.charAt(j) !== ch) {
          j++;
        }
        out = out + sql.slice(i, j + 1);
        i = j + 1;
        continue;
      }
      if (isWordStart(ch) && (i === 0 || !isWord(sql.charAt(i - 1)))) {
        let j = i;
        while (j < n && isWord(sql.charAt(j))) {
          j++;
        }
        const word = sql.slice(i, j);
        if (j < n && sql.charAt(j) === "." && this.aliases.has(word)) {
          let k = j + 1;
          while (k < n && isWord(sql.charAt(k))) {
            k++;
          }
          const prop = sql.slice(j + 1, k);
          const m = this.aliases.get(word);
          if (m !== null && prop.length > 0) {
            // UPDATE and DELETE name the table without an alias: the column alone, as TypeORM writes it
            const bare = this.mode === "update" || this.mode === "delete";
            out = out + (bare ? "" : quote(word) + ".") + quote(columnName(m, prop));
            i = k;
            continue;
          }
        }
        out = out + word;
        i = j;
        continue;
      }
      out = out + ch;
      i++;
    }
    return out;
  }

  // :name and :...name (outside quotes and :: casts) -> the dialect's numbered binds
  private bindsIn(sql: string, binds: Binds): string {
    const params: object = JSON.parse(this.params);
    let out = "";
    let i = 0;
    const n = sql.length;
    while (i < n) {
      const ch = sql.charAt(i);
      if (ch === "'") {
        let j = i + 1;
        while (j < n && sql.charAt(j) !== "'") {
          j++;
        }
        out = out + sql.slice(i, j + 1);
        i = j + 1;
        continue;
      }
      if (ch === ":" && i + 1 < n && sql.charAt(i + 1) === ":") {
        out = out + "::";
        i += 2;
        continue;
      }
      if (ch === ":" && i + 1 < n && (isWordStart(sql.charAt(i + 1)) || sql.charAt(i + 1) === ".")) {
        let j = i + 1;
        let spread = false;
        if (sql.slice(j, j + 3) === "...") {
          spread = true;
          j += 3;
        }
        let k = j;
        while (k < n && isWord(sql.charAt(k))) {
          k++;
        }
        const name = sql.slice(j, k);
        const v = get(params, name);
        if (spread || kind(v) === "array") {
          const parts: string[] = [];
          const m = len(v);
          for (let x = 0; x < m; x++) {
            parts.push(binds.addJson(at(v, x)));
          }
          out = out + parts.join(", ");
        } else {
          out = out + binds.addJson(v);
        }
        i = k;
        continue;
      }
      out = out + ch;
      i++;
    }
    return out;
  }

  private wherePart(binds: Binds): string {
    if (this.wheres.length === 0) {
      return "";
    }
    return " WHERE " + this.bindsIn(this.columnsIn(this.wheres.join(" ")), binds);
  }

  private fromPart(binds: Binds): string {
    let s = " FROM " + this.table + (this.mainAlias.length > 0 ? " " + quote(this.mainAlias) : "");
    for (const j of this.joins) {
      s = s + " " + j.kind + " " + j.target + " " + quote(j.alias);
      if (j.condition.length > 0) {
        s = s + " ON " + this.bindsIn(this.columnsIn(j.condition), binds);
      }
    }
    return s;
  }

  private selectPart(): string {
    if (this.selects.length === 0) {
      const m = this.mainMeta;
      if (m !== null) {
        return selectList(m, this.mainAlias, []);
      }
      return "*";
    }
    const parts: string[] = [];
    for (const s of this.selects) {
      // "alias.prop" alone: TypeORM's raw name alias_COLUMN
      const dot = s.indexOf(".");
      if (dot > 0 && s.indexOf(" ") < 0 && s.indexOf("(") < 0) {
        const a = s.slice(0, dot);
        const p = s.slice(dot + 1);
        const m = this.aliases.get(a);
        if (m !== null) {
          const col = columnName(m, p);
          parts.push(quote(a) + "." + quote(col) + " AS " + quote(a + "_" + col));
          continue;
        }
      }
      parts.push(this.columnsIn(s));
    }
    return parts.join(", ");
  }

  /** The statement and its parameters as they will run (for tests and logging). */
  getQueryAndParameters(): string {
    const binds = new Binds(this.executor.dialect());
    return this.build(binds);
  }

  getSql(): string {
    return this.getQueryAndParameters();
  }

  private build(binds: Binds): string {
    if (this.mode === "update") {
      const m = this.mainMeta;
      const vals: object = JSON.parse(this.setValues);
      const props: string[] = JSON.parse(JSON.stringify(keys(vals)));
      const sets: string[] = [];
      for (const p of props) {
        const col = m === null ? p : columnName(m, p);
        const meta = m === null ? null : m.findColumn(p);
        sets.push(quote(col) + " = " + binds.addColumnJson(get(vals, p), meta === null ? "" : columnKind(meta)));
      }
      return "UPDATE " + this.table + " SET " + sets.join(", ") + this.wherePart(binds);
    }
    if (this.mode === "delete") {
      return "DELETE FROM " + this.table + this.wherePart(binds);
    }
    if (this.mode === "insert") {
      const m = this.mainMeta;
      const rows: object = JSON.parse(this.insertValues);
      const n = len(rows);
      if (n === 0) {
        throw new Error("insert().values(...): no values");
      }
      const props: string[] = JSON.parse(JSON.stringify(keys(at(rows, 0))));
      const cols: string[] = [];
      for (const p of props) {
        cols.push(quote(m === null ? p : columnName(m, p)));
      }
      const tuples: string[] = [];
      for (let r = 0; r < n; r++) {
        const row = at(rows, r);
        const vs: string[] = [];
        for (const p of props) {
          vs.push(binds.addJson(get(row, p)));
        }
        tuples.push("(" + vs.join(", ") + ")");
      }
      if (binds.dialect === "oracle" && tuples.length > 1) {
        // Oracle has no multi-row VALUES: INSERT ALL INTO ... VALUES (...) ... SELECT 1 FROM DUAL
        let s = "INSERT ALL";
        for (const t of tuples) {
          s = s + " INTO " + this.table + " (" + cols.join(", ") + ") VALUES " + t;
        }
        return s + " SELECT 1 FROM DUAL";
      }
      return "INSERT INTO " + this.table + " (" + cols.join(", ") + ") VALUES " + tuples.join(", ");
    }
    let sql = "SELECT " + this.selectPart() + this.fromPart(binds) + this.wherePart(binds);
    if (this.groups.length > 0) {
      sql = sql + " GROUP BY " + this.columnsIn(this.groups.join(", "));
    }
    if (this.orders.length > 0) {
      sql = sql + " ORDER BY " + this.columnsIn(this.orders.join(", "));
    }
    return sql + pageSql(binds.dialect, this.skipN, this.takeN);
  }

  async getRawMany(): Promise<object> {
    const binds = new Binds(this.executor.dialect());
    const sql = this.build(binds);
    return this.executor.rows(sql, binds.values);
  }

  async getRawOne(): Promise<object | null> {
    const rows = await this.getRawMany();
    if (len(rows) === 0) {
      return null;
    }
    return at(rows, 0);
  }

  async getMany(): Promise<T[]> {
    const m = this.mainMeta;
    if (m === null) {
      throw new Error("getMany() needs an entity: createQueryBuilder(Entity, alias) or repository.createQueryBuilder(alias)");
    }
    const rows = await this.getRawMany();
    const mapped = remap(rows, m.spec());
    const out: T[] = JSON.parse(JSON.stringify(mapped));
    return out;
  }

  async getOne(): Promise<T | null> {
    const many = await this.getMany();
    return many.length > 0 ? many[0] : null;
  }

  async getCount(): Promise<int> {
    const binds = new Binds(this.executor.dialect());
    const sql = 'SELECT COUNT(*) AS "cnt"' + this.fromPart(binds) + this.wherePart(binds);
    return countOf(await this.executor.rows(sql, binds.values));
  }

  async execute(): Promise<object> {
    const binds = new Binds(this.executor.dialect());
    const sql = this.build(binds);
    const r = await this.executor.run(sql, binds.values);
    if (this.mode === "select") {
      return get(r, "rows");
    }
    const text = '{"affected":' + JSON.stringify(get(r, "rowCount")) + ',"raw":' + JSON.stringify(r) + "}";
    const o: object = JSON.parse(text);
    return o;
  }
}

function isWordStart(ch: string): boolean {
  const c = ch.charCodeAt(0);
  return (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c === 95;
}
function isWord(ch: string): boolean {
  const c = ch.charCodeAt(0);
  return isWordStart(ch) || (c >= 48 && c <= 57);
}

// ---------------------------------------------------------------------------------------------------------------
// EntityManager, QueryRunner, DataSource

export class EntityManager {
  private executor: Executor;
  constructor(executor: Executor) {
    this.executor = executor;
  }
  get connection(): DataSource {
    return this.executor.dataSource;
  }
  async query<T = any>(sql: string, params: unknown[] = []): Promise<T> {
    const raw: object = await this.executor.rows(sql, params);
    const rows: T = raw; // query<Row[]>(...) decodes the rows as Row[], like TypeORM's query<T>
    return rows;
  }
  /**
   * TypeORM's QueryResult ({ records, affected, raw }), what queryRunner.query(sql, params, true) returns: for
   * Oracle the rows of a query, rowsAffected of DML, or a PL/SQL block's outBinds (records when by position)
   */
  async queryResult<T = object>(sql: string, params: unknown[] = []): Promise<T> {
    const r = await this.executor.run(sql, params);
    const rows = get(r, "rows");
    const out = get(r, "outBinds");
    const ra = get(r, "rowsAffected");
    let text = "";
    if (this.executor.dialect() !== "oracle") {
      text = '{"records":' + JSON.stringify(rows) + ',"affected":' + JSON.stringify(get(r, "rowCount")) + ',"raw":' + JSON.stringify(rows) + "}";
    } else if (kind(out) !== "null") {
      text = '{"records":' + (kind(out) === "array" ? JSON.stringify(out) : "[]") + ',"raw":' + JSON.stringify(out) + "}";
    } else if (kind(ra) === "number") {
      text = '{"records":[],"affected":' + JSON.stringify(ra) + ',"raw":' + JSON.stringify(ra) + "}";
    } else {
      text = '{"records":' + JSON.stringify(rows) + ',"raw":' + JSON.stringify(rows) + "}";
    }
    const o: T = JSON.parse(text);
    return o;
  }
  /** getRepository<User>(User): the class gives its metadata, the type argument the entities' type */
  getRepository<T>(entity: ClassInfo): Repository<T> {
    return new Repository<T>(this.executor.dataSource.options.name ?? "default", this.executor.sessionId);
  }
  createQueryBuilder<T>(entity: ClassInfo, alias: string): SelectQueryBuilder<T> {
    const qb = new SelectQueryBuilder<T>(this.executor);
    qb.from(entity, alias);
    return qb;
  }
}

export class QueryRunner {
  private dataSource: DataSource;
  private sessionOf: DriverSession | null = null;
  isTransactionActive: boolean = false;
  isReleased: boolean = false;
  manager: EntityManager;

  constructor(dataSource: DataSource) {
    this.dataSource = dataSource;
    this.manager = new EntityManager(new Executor(dataSource, null));
  }

  private sessionId: int = -1;
  /** the driver's connection once connected (an oracledb Connection for "oracle"), as in TypeORM */
  databaseConnection: unknown = null;

  /** takes a connection for this runner (once); returns the driver's connection, as TypeORM does */
  async connect(): Promise<unknown> {
    if (!this.dataSource.isInitialized) {
      await this.dataSource.initialize();
    }
    if (this.sessionOf === null) {
      const s = await this.dataSource.driver().session();
      this.sessionOf = s;
      this.sessionId = nextSession;
      nextSession = nextSession + 1;
      sessions.set(this.sessionId, s);
      this.manager = new EntityManager(new Executor(this.dataSource, s, this.sessionId));
      this.databaseConnection = s.raw;
    }
    return this.databaseConnection;
  }

  private session(): DriverSession {
    const s = this.sessionOf;
    if (s === null) {
      throw new Error("QueryRunner: call connect() first");
    }
    return s;
  }

  async startTransaction(): Promise<void> {
    await this.connect();
    await this.session().begin();
    this.isTransactionActive = true;
  }

  async commitTransaction(): Promise<void> {
    await this.session().commit();
    this.isTransactionActive = false;
  }

  async rollbackTransaction(): Promise<void> {
    await this.session().rollback();
    this.isTransactionActive = false;
  }

  async release(): Promise<void> {
    const s = this.sessionOf;
    if (s !== null) {
      await s.release();
      sessions.delete(this.sessionId);
    }
    this.sessionOf = null;
    this.databaseConnection = null;
    this.isReleased = true;
  }

  /** raw SQL on this runner's connection; useStructuredResult: TypeORM's QueryResult (see EntityManager.queryResult) */
  async query<T = any>(sql: string, params: unknown[] = [], useStructuredResult: boolean = false): Promise<T> {
    await this.connect();
    if (useStructuredResult) {
      return this.manager.queryResult<T>(sql, params);
    }
    return this.manager.query<T>(sql, params);
  }
}

export class DataSource {
  readonly options: DataSourceOptions;
  driverOf: Driver | null = null; // (set by withDriver)
  isInitialized: boolean = false;
  queryWrappers: QueryWrapper[] = [];
  manager: EntityManager;

  constructor(options: DataSourceOptions) {
    this.options = options;
    this.manager = new EntityManager(new Executor(this, null));
    registerDataSource(this);
  }

  /** the driver of options.type (postgres built in; others from registerDriver) */
  driver(): Driver {
    const have = this.driverOf;
    if (have !== null) {
      return have;
    }
    const f = driverFactories.get(this.options.type);
    let made: Driver = new PostgresDriver(this.options);
    if (f !== null) {
      made = f.create(this.options);
    } else if (this.options.type === "oracle") {
      const o = this.options;
      made = new OracleDriver(async (): Promise<OraclePoolFunctions> => oraclePool(o));
    } else if (this.options.type !== "postgres") {
      throw new Error(`DataSource: no driver for type "${this.options.type}" (install the package that registers it, e.g. oracledb)`);
    }
    this.driverOf = made;
    return made;
  }

  /** a DataSource over a given driver (tests, or a driver built by hand) */
  static withDriver(options: DataSourceOptions, driver: Driver): DataSource {
    const ds = new DataSource(options);
    ds.driverOf = driver;
    return ds;
  }

  async initialize(): Promise<DataSource> {
    await this.driver().connect();
    this.isInitialized = true;
    return this;
  }

  async destroy(): Promise<void> {
    await this.driver().close();
    this.isInitialized = false;
  }

  /** raw SQL with the dialect's binds ($1 for postgres, :1 for oracle); the rows as an object to decode */
  async query<T = any>(sql: string, params: unknown[] = []): Promise<T> {
    return this.manager.query<T>(sql, params);
  }

  /** getRepository<User>(User): the class gives its metadata, the type argument the entities' type */
  getRepository<T>(entity: ClassInfo): Repository<T> {
    return new Repository<T>(this.options.name ?? "default", -1);
  }

  createQueryBuilder<T>(entity: ClassInfo, alias: string): SelectQueryBuilder<T> {
    return this.manager.createQueryBuilder<T>(entity, alias);
  }

  createQueryRunner(): QueryRunner {
    return new QueryRunner(this);
  }

  /** xisto: wraps every statement this DataSource runs (the last added runs first); see QueryWrapper */
  wrapQueries(w: QueryWrapper): void {
    this.queryWrappers.push(w);
  }

  /** runs fn in a transaction: committed when it returns, rolled back when it throws (the error goes on) */
  async transaction(fn: (manager: EntityManager) => Promise<void>): Promise<void> {
    const qr = this.createQueryRunner();
    await qr.startTransaction();
    try {
      await fn(qr.manager);
      await qr.commitTransaction();
    } catch (e) {
      await qr.rollbackTransaction();
      throw e;
    } finally {
      await qr.release();
    }
  }
}
