// oracledb: the node-oracledb contract in thin mode, an Oracle Database client with no Oracle Client libraries
// (native/oracle.c speaks Oracle Net and TTC over TCP). Ported from node-oracledb 6.10 thin, dual-licensed
// Apache-2.0 / UPL-1.0, Copyright (c) Oracle and/or its affiliates.
//
// A result is JSON that a typed declaration decodes, as with pg:
//   const r: Result<Row> = await conn.execute("select id, name from users where id = :1", [7], { outFormat: OUT_FORMAT_OBJECT });

/** Rows as arrays of column values, in select-list order (node-oracledb's default). */
export const OUT_FORMAT_ARRAY: int = 4001;
/** Rows as objects keyed by column name (upper case unless the SQL quotes the alias). */
export const OUT_FORMAT_OBJECT: int = 4002;

/** Database type codes, as node-oracledb numbers them (for fetchAsString, fetchAsBuffer and metadata). */
export const DB_TYPE_VARCHAR: int = 2001;
export const DB_TYPE_RAW: int = 2006;
export const DB_TYPE_NUMBER: int = 2010;
export const DB_TYPE_DATE: int = 2011;
export const DB_TYPE_TIMESTAMP: int = 2012;
export const DB_TYPE_CLOB: int = 2017;
export const DB_TYPE_BLOB: int = 2019;
export const DB_TYPE_BOOLEAN: int = 2022;
export const DB_TYPE_JSON: int = 2027;

/** Bind directions and the type aliases node-oracledb exports (for { dir, type } bind descriptors). */
export const BIND_IN: int = 3001;
export const BIND_INOUT: int = 3002;
export const BIND_OUT: int = 3003;
export const STRING: int = 2001;
export const NUMBER: int = 2010;
export const DATE: int = 2011;
export const CURSOR: int = 2021;
export const BUFFER: int = 2006;
export const CLOB: int = 2017;
export const BLOB: int = 2019;
export const DB_TYPE_CURSOR: int = 2021;

/**
 * The column types fetched as text. xisto always fetches CLOB as text (what TypeORM asks with
 * `fetchAsString = [DB_TYPE_CLOB]`), so the setting is accepted and has no other effect.
 * Compatibility: node-oracledb returns Lob objects unless CLOB is listed here.
 */
export let fetchAsString: int[] = [];
/**
 * The column types fetched as bytes. xisto always fetches BLOB whole, as lowercase hex text.
 * Compatibility: node-oracledb returns a Buffer for the types listed here (a Lob object otherwise).
 */
export let fetchAsBuffer: int[] = [];

/**
 * Rows fetched ahead with a query's first round trip, and rows per later round trip (node-oracledb's
 * oracledb.prefetchRows and oracledb.fetchArraySize, set by assignment).
 * Compatibility: accepted; the thin driver here fetches with its own sizes.
 */
export let prefetchRows: number = 2;
export let fetchArraySize: number = 100;

/**
 * Thick mode (Oracle Client libraries) is not available: xisto's driver is thin only. Throws as node-oracledb
 * does on a machine with no Oracle Client, so code that falls back to thin mode keeps working.
 */
export function initOracleClient(): void {
  throw new Error("DPI-1047: Cannot locate an Oracle Client library: xisto's oracledb runs in thin mode only");
}

/** How to reach the database and who logs in. */
export interface ConnectionAttributes {
  /** The database user. */
  user?: string;
  /** The user's password (sent only as the O5LOGON key exchange derives it; never logged). */
  password?: string;
  /**
   * Where the database listens: Easy Connect (`host[:port][/service]`, port 1521 by default) or a full
   * `(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=...)(PORT=...))(CONNECT_DATA=(SERVICE_NAME=...)))` descriptor.
   */
  connectString?: string;
  /** Another name for connectString. */
  connectionString?: string;
}

/** A pool's attributes: the connection's, and how many connections it keeps. */
export interface PoolAttributes extends ConnectionAttributes {
  /** The most connections open at once (4 by default; split among the program's worker threads). */
  poolMax?: int;
  /** Accepted for compatibility: connections open on demand. */
  poolMin?: int;
  /** Accepted for compatibility: connections open one at a time. */
  poolIncrement?: int;
  /**
   * Runs once on each new physical connection, before its first use: what it sets on the connection
   * (callTimeout) stays for every later checkout of it. callback() when done, callback(err) to fail getConnection.
   * Compatibility: the connection it receives has callTimeout (node-oracledb's whole Connection otherwise); a
   * string naming a PL/SQL procedure is not supported.
   */
  sessionCallback?: (connection: SessionConnection, requestedTag: string, callback: (err?: Error) => void) => void;
}

/** What a sessionCallback sets on a new connection. */
export interface SessionConnection {
  /** The most milliseconds a round trip may take (0: no limit). */
  callTimeout: number;
}

/** Options of one execute. */
export interface ExecuteOptions {
  /** OUT_FORMAT_OBJECT for rows as objects, OUT_FORMAT_ARRAY (the default) for arrays. */
  outFormat?: int;
  /** Commit the transaction after a DML statement succeeds (false by default). */
  autoCommit?: boolean;
  /** The most rows a query returns (0, the default, for all). */
  maxRows?: int;
}

/** A column of a query's result. */
export interface Metadata {
  /** The column name as the database reports it. */
  name: string;
}

/**
 * A bind given as an object, node-oracledb's bind parameter: the direction, the type and, for OUT strings and
 * bytes, the most bytes the value may take.
 * @example
 *   const msg: BindParameter = { dir: BIND_OUT, type: STRING, maxSize: 4000 };
 *   const r: Result<object, { MSG: string | null }> = await conn.execute("begin :msg := pkg.fn(:id); end;", { id: 7, msg });
 */
export interface BindParameter {
  /** BIND_IN (the default), BIND_INOUT or BIND_OUT. */
  dir?: int;
  /** STRING (the default for OUT binds), NUMBER, DATE, DB_TYPE_TIMESTAMP, CLOB, BLOB, BUFFER or DB_TYPE_BOOLEAN. */
  type?: int;
  /**
   * For OUT and IN OUT strings and bytes: the most bytes the value takes (200 by default). A PL/SQL bind over
   * 32767 bytes goes as a CLOB / BLOB and comes back whole.
   */
  maxSize?: int;
  /** The value of an IN or IN OUT bind. */
  val?: unknown;
}

/**
 * What execute returns, decoded from JSON by the declared row type (T) and OUT binds type (O).
 * NUMBER arrives as a number, VARCHAR2/CHAR/CLOB as text, DATE/TIMESTAMP as a Date (read in the local time zone,
 * as node-oracledb does; the TIME ZONE kinds as UTC), RAW/BLOB as lowercase hex text.
 */
export interface Result<T, O = object> {
  /** The rows of a query (empty for other statements). */
  rows: T[];
  /** The columns of a query (empty for other statements). */
  metaData: Metadata[];
  /** Rows inserted, updated or deleted by a DML statement (absent for queries and PL/SQL blocks). */
  rowsAffected?: int;
  /**
   * The values of the OUT and IN OUT binds of a PL/SQL block: an object by bind name when the binds were given by
   * name, an array in bind order when by position (absent when there are none).
   * Compatibility: node-oracledb gives a CLOB or BLOB OUT bind as a Lob; xisto gives its whole value (text, or
   * lowercase hex for a BLOB), as lob.getData() would.
   */
  outBinds?: O;
}

declare class NativePool {
  constructor(config: object);
  acquire(): int;
  release(handle: int): void;
  execute(handle: int, sql: string, values: unknown[], names: string[], dirs: int[], types: int[], sizes: int[], objects: boolean, autoCommit: boolean, maxRows: int): object;
  commit(handle: int): void;
  rollback(handle: int): void;
  ping(handle: int): void;
  close(): void;
  lastOffset(): int;
  takeFresh(handle: int): boolean;
  callTimeout(handle: int): int;
  setCallTimeout(handle: int, ms: int): void;
}

/**
 * An error of the database (ORA-) or of the driver (NJS-, DPI-), with node-oracledb's fields.
 * @example
 *   try { await conn.execute("begin pkg.p(:1); end;", [7]); }
 *   catch (e) { if (e instanceof OracleError && e.errorNum >= 20000 && e.errorNum <= 20999) ... }
 */
export class OracleError extends Error {
  /** The ORA- number (20001 for ORA-20001); 0 for a driver error. Compatibility: node-oracledb leaves it undefined then. */
  errorNum: int = 0;
  /** "ORA-20001", "NJS-016", "DPI-1047". */
  code: string = "";
  /** Where in the SQL the error is (0 when the database does not say). */
  offset: int = 0;
  /** Whether the session can go on after it (false: xisto does not tell). */
  isRecoverable: boolean = false;
  constructor(message: string, offset: int) {
    super(message);
    this.name = "Error";
    const m = /^([A-Z]{3})-(\d{3,5}):/.exec(message);
    if (m !== null) {
      this.code = m[1] + "-" + m[2];
      if (m[1] === "ORA") this.errorNum = Math.trunc(parseInt(m[2], 10));
    }
    this.offset = offset;
  }
}

// throws a database or driver error as an OracleError (other errors as they are)
function rethrow(e: unknown, native: NativePool): void {
  if (!(e instanceof Error)) throw new Error(String(e)); // (the native driver throws Errors only)
  const err = e as Error;
  if (err instanceof OracleError) throw err;
  const msg = err.message;
  if (!/^(ORA|NJS|DPI)-\d{3,5}:/.test(msg)) throw err;
  throw new OracleError(msg, msg.startsWith("ORA-") ? native.lastOffset() : 0);
}

// the binds of one execute, as the native driver takes them: values, and each bind parameter object's fields
class Binds {
  values: unknown[] = [];
  names: string[] = [];
  dirs: int[] = [];
  types: int[] = [];
  sizes: int[] = [];

  add(name: string, v: unknown): void {
    if (name.length > 0) this.names.push(name);
    if (v !== null && typeof v === "object") {
      const u: any = v;
      if ("dir" in u || "type" in u || "maxSize" in u || "val" in u) {
        const dir: int = u.dir ?? BIND_IN;
        const type: int = u.type ?? 0;
        const size: int = u.maxSize ?? -1;
        const val: unknown = u.val;
        if (dir !== BIND_IN && dir !== BIND_INOUT && dir !== BIND_OUT) throw new Error("NJS-013: invalid bind direction");
        if (dir === BIND_OUT) this.values.push(null);
        else this.values.push(val);
        this.dirs.push(dir);
        this.types.push(type);
        this.sizes.push(size);
        return;
      }
    }
    this.values.push(v);
    this.dirs.push(BIND_IN);
    this.types.push(0);
    this.sizes.push(-1);
  }
}

// binds by position (an array) or by name (a Record): node-oracledb's execute takes either
function bindsByPosition(binds: unknown[]): Binds {
  const b = new Binds();
  for (const v of binds) b.add("", v);
  return b;
}
function bindsByName(binds: Record<string, unknown>): Binds {
  const b = new Binds();
  for (const k of Object.keys(binds)) b.add(k, binds[k]);
  return b;
}

/**
 * A session with the database, from a pool or standalone.
 * Statements in a connection run one at a time; a transaction stays open until commit, rollback or close (which
 * rolls back), unless autoCommit is set.
 */
export class Connection {
  private native: NativePool;
  private handle: int;
  private standalone: boolean;
  private open: boolean = true;

  constructor(native: NativePool, handle: int, standalone: boolean) {
    this.native = native;
    this.handle = handle;
    this.standalone = standalone;
  }

  /**
   * Runs one SQL statement or PL/SQL block.
   * @param sql the statement, with bind placeholders such as `:1` or `:name`
   * @param binds the values for the placeholders, in the order they appear (strings, numbers, booleans as 1/0,
   *   Dates, Uint8Arrays, null or BindParameter objects), or an object of names and values for binds by name
   * @param options outFormat, autoCommit, maxRows
   * @returns the rows and columns of a query, rowsAffected for DML, outBinds for a PL/SQL block with OUT binds
   * @throws Error with the database's message ("ORA-00942: table or view does not exist ...") or a driver one
   *   ("NJS-098: ..." when the number of values does not match the placeholders, "NJS-016: buffer is too small for
   *   OUT binds" when an OUT value is longer than its maxSize)
   * @example
   *   const r: Result<{ ID: number }> = await conn.execute("select id from t where name = :1", ["ana"], { outFormat: OUT_FORMAT_OBJECT });
   *   const ret: BindParameter = { dir: BIND_OUT, type: NUMBER };
   *   const f: Result<object, number[]> = await conn.execute("begin :1 := pkg.total(:2); end;", [ret, 7]);
   * Compatibility: as node-oracledb thin, except that CLOB/BLOB always come whole (also as OUT binds), and that
   * RETURNING INTO and REF CURSOR binds are not supported yet.
   */
  async execute<T = any, O = any>(sql: string, binds: unknown[] | Record<string, unknown> = [], options: ExecuteOptions = {}): Promise<Result<T, O>> {
    // (T and O from the target, `const r: Result<Row> = await conn.execute(...)`, or any: result.rows[0][0])
    const r: Result<T, O> = await this.executeRaw(sql, binds, options);
    return r;
  }

  /** execute's result as the driver gives it (JSON): what a library that reads it generically takes (typeorm). */
  async executeRaw(sql: string, binds: unknown[] | Record<string, unknown> = [], options: ExecuteOptions = {}): Promise<object> {
    if (!Array.isArray(binds)) {
      return this.executeNamedRaw(sql, binds, options); // binds by name, as node-oracledb takes them
    }
    this.check();
    this.checkCallTimeout();
    const b = bindsByPosition(binds);
    try {
      return this.native.execute(this.handle, sql, b.values, b.names, b.dirs, b.types, b.sizes, (options.outFormat ?? OUT_FORMAT_ARRAY) === OUT_FORMAT_OBJECT, options.autoCommit ?? false, options.maxRows ?? 0);
    } catch (e) {
      rethrow(e, this.native);
      throw e;
    }
  }

  /**
   * Runs one statement with binds by name: each `:name` placeholder takes the value of the same key
   * (case-insensitive), so a name used twice takes the same value.
   * @param sql the statement
   * @param binds an object of names and values (strings, numbers, booleans, Dates, Uint8Arrays, null or
   *   BindParameter objects)
   * @param options outFormat, autoCommit, maxRows
   * @returns as execute; outBinds by name
   * @throws Error "NJS-097: no bind placeholder named: :X" when a placeholder has no value
   * @example
   *   await conn.executeNamed("update t set name = :name where id = :id", { id: 7, name: "ana" }, { autoCommit: true });
   *   const msg: BindParameter = { dir: BIND_OUT, type: STRING, maxSize: 4000 };
   *   const r: Result<object, { msg: string | null }> = await conn.executeNamed("begin pkg.p(:id, :msg); end;", { id: 7, msg });
   * Compatibility: node-oracledb takes the object as execute's second argument.
   */
  async executeNamed<T = any, O = any>(sql: string, binds: Record<string, unknown>, options: ExecuteOptions = {}): Promise<Result<T, O>> {
    const r: Result<T, O> = await this.executeNamedRaw(sql, binds, options);
    return r;
  }

  /** executeNamed's result as the driver gives it (JSON). */
  async executeNamedRaw(sql: string, binds: Record<string, unknown>, options: ExecuteOptions = {}): Promise<object> {
    this.check();
    this.checkCallTimeout();
    const b = bindsByName(binds);
    try {
      return this.native.execute(this.handle, sql, b.values, b.names, b.dirs, b.types, b.sizes, (options.outFormat ?? OUT_FORMAT_ARRAY) === OUT_FORMAT_OBJECT, options.autoCommit ?? false, options.maxRows ?? 0);
    } catch (e) {
      rethrow(e, this.native);
      throw e;
    }
  }

  /** Commits the current transaction. @throws Error on a database error */
  async commit(): Promise<void> {
    this.check();
    try {
      this.native.commit(this.handle);
    } catch (e) {
      rethrow(e, this.native);
      throw e;
    }
  }

  /** Rolls back the current transaction. @throws Error on a database error */
  async rollback(): Promise<void> {
    this.check();
    try {
      this.native.rollback(this.handle);
    } catch (e) {
      rethrow(e, this.native);
      throw e;
    }
  }

  /** Checks that the session is alive with a round trip. @throws Error when it is not */
  async ping(): Promise<void> {
    this.check();
    try {
      this.native.ping(this.handle);
    } catch (e) {
      rethrow(e, this.native);
      throw e;
    }
  }

  /**
   * Releases the connection: back to its pool (after a rollback), or logged off when standalone.
   * Compatibility: release() is the same.
   */
  async close(): Promise<void> {
    if (!this.open) return;
    this.open = false;
    this.native.release(this.handle);
    if (this.standalone) this.native.close();
  }

  /** The same as close. */
  async release(): Promise<void> {
    await this.close();
  }

  private check(): void {
    if (!this.open) throw new Error("NJS-003: invalid or closed connection");
  }

  /**
   * The most milliseconds a round trip may take (0, the default: no limit); it stays with the physical connection
   * across checkouts. Compatibility: a limit above 0 is not enforced yet, so a statement on such a connection
   * fails (never runs past a limit silently).
   */
  get callTimeout(): number {
    return this.native.callTimeout(this.handle);
  }

  set callTimeout(ms: number) {
    this.native.setCallTimeout(this.handle, Math.trunc(ms));
  }

  private checkCallTimeout(): void {
    const ms = this.native.callTimeout(this.handle);
    if (ms > 0) throw new Error(`NJS-123: callTimeout of ${ms} ms is not supported by xisto's oracledb yet: set it to 0`);
  }

  /** (the pool's) true once: the first checkout of a new physical connection. */
  takeFresh(): boolean {
    return this.native.takeFresh(this.handle);
  }
}

/** A set of open sessions that connections are taken from and given back to. */
export class Pool {
  private native: NativePool;
  private sessionCallback: ((connection: SessionConnection, requestedTag: string, callback: (err?: Error) => void) => void) | null;

  constructor(attrs: PoolAttributes) {
    this.native = new NativePool({ user: attrs.user, password: attrs.password, connectString: attrs.connectString ?? attrs.connectionString, poolMax: attrs.poolMax });
    this.sessionCallback = attrs.sessionCallback ?? null;
  }

  /**
   * Takes a connection from the pool (opening one, or waiting for one when poolMax are in use).
   * @returns a Connection to close when done
   * @throws Error when the database cannot be reached or the login is refused
   */
  async getConnection(): Promise<Connection> {
    let handle: int = 0;
    try {
      handle = this.native.acquire();
    } catch (e) {
      rethrow(e, this.native);
      throw e;
    }
    const conn = new Connection(this.native, handle, false);
    const fn = this.sessionCallback;
    if (fn !== null && conn.takeFresh()) {
      // node-oracledb's sessionCallback: once per new physical connection, before it is handed out
      const view: SessionConnection = { callTimeout: conn.callTimeout };
      try {
        await new Promise<void>((resolve, reject) => {
          fn(view, "", (err?: Error): void => {
            if (err) reject(err);
            else resolve();
          });
        });
      } catch (e) {
        await conn.close();
        throw e;
      }
      conn.callTimeout = view.callTimeout;
    }
    return conn;
  }

  /** Logs off the pool's idle connections. */
  async close(): Promise<void> {
    this.native.close();
  }
}

/**
 * Creates a pool of connections.
 * @param attrs user, password, connectString, poolMax
 * @returns the Pool (connections open when taken)
 * @example
 *   const pool = await createPool({ user: "app", password: secret, connectString: "db:1521/ORCLPDB1", poolMax: 8 });
 */
export async function createPool(attrs: PoolAttributes): Promise<Pool> {
  return new Pool(attrs);
}

/**
 * Opens a standalone connection.
 * @param attrs user, password, connectString
 * @returns the Connection
 * @throws Error when the database cannot be reached or the login is refused ("ORA-01017: invalid credential ...")
 * @example
 *   const conn = await getConnection({ user: "app", password: secret, connectString: "localhost/FREEPDB1" });
 */
export async function getConnection(attrs: ConnectionAttributes): Promise<Connection> {
  const native = new NativePool({ user: attrs.user, password: attrs.password, connectString: attrs.connectString ?? attrs.connectionString, poolMax: 1 });
  let handle: int = 0; // (its own statement: a throw inside the return's arguments would leak native)
  try {
    handle = native.acquire();
  } catch (e) {
    rethrow(e, native);
    throw e;
  }
  return new Connection(native, handle, true);
}
