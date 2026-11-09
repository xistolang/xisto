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
 * What execute returns, decoded from JSON by the declared row type.
 * NUMBER arrives as a number, VARCHAR2/CHAR/CLOB as text, DATE/TIMESTAMP as a Date (read in the local time zone,
 * as node-oracledb does; the TIME ZONE kinds as UTC), RAW/BLOB as lowercase hex text.
 */
export interface Result<T> {
  /** The rows of a query (empty for other statements). */
  rows: T[];
  /** The columns of a query (empty for other statements). */
  metaData: Metadata[];
  /** Rows inserted, updated or deleted by a DML statement (absent for queries). */
  rowsAffected?: int;
}

declare class NativePool {
  constructor(config: object);
  acquire(): int;
  release(handle: int): void;
  execute(handle: int, sql: string, values: unknown[], named: string, objects: boolean, autoCommit: boolean, maxRows: int): object;
  commit(handle: int): void;
  rollback(handle: int): void;
  ping(handle: int): void;
  close(): void;
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
   *   Dates, Uint8Arrays or null)
   * @param options outFormat, autoCommit, maxRows
   * @returns the rows and columns of a query, or rowsAffected for DML
   * @throws Error with the database's message ("ORA-00942: table or view does not exist ...") or a driver one
   *   ("NJS-098: ..." when the number of values does not match the placeholders)
   * @example
   *   const r: Result<{ ID: number }> = await conn.execute("select id from t where name = :1", ["ana"], { outFormat: OUT_FORMAT_OBJECT });
   * Compatibility: as node-oracledb thin, except that CLOB/BLOB always come whole and OUT binds are not supported yet.
   */
  async execute(sql: string, binds: unknown[] = [], options: ExecuteOptions = {}): Promise<object> {
    this.check();
    return this.native.execute(this.handle, sql, binds, "", (options.outFormat ?? OUT_FORMAT_ARRAY) === OUT_FORMAT_OBJECT, options.autoCommit ?? false, options.maxRows ?? 0);
  }

  /**
   * Runs one statement with binds by name: each `:name` placeholder takes the value of the same key
   * (case-insensitive), so a name used twice takes the same value.
   * @param sql the statement
   * @param binds an object of names and values (strings, numbers, booleans or null)
   * @param options outFormat, autoCommit, maxRows
   * @returns as execute
   * @throws Error "NJS-097: no bind placeholder named: :X" when a placeholder has no value
   * @example
   *   await conn.executeNamed("update t set name = :name where id = :id", { id: 7, name: "ana" }, { autoCommit: true });
   * Compatibility: node-oracledb takes the object as execute's second argument.
   */
  async executeNamed(sql: string, binds: object, options: ExecuteOptions = {}): Promise<object> {
    this.check();
    return this.native.execute(this.handle, sql, [], JSON.stringify(binds), (options.outFormat ?? OUT_FORMAT_ARRAY) === OUT_FORMAT_OBJECT, options.autoCommit ?? false, options.maxRows ?? 0);
  }

  /** Commits the current transaction. @throws Error on a database error */
  async commit(): Promise<void> {
    this.check();
    this.native.commit(this.handle);
  }

  /** Rolls back the current transaction. @throws Error on a database error */
  async rollback(): Promise<void> {
    this.check();
    this.native.rollback(this.handle);
  }

  /** Checks that the session is alive with a round trip. @throws Error when it is not */
  async ping(): Promise<void> {
    this.check();
    this.native.ping(this.handle);
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
}

/** A set of open sessions that connections are taken from and given back to. */
export class Pool {
  private native: NativePool;

  constructor(attrs: PoolAttributes) {
    this.native = new NativePool(attrs);
  }

  /**
   * Takes a connection from the pool (opening one, or waiting for one when poolMax are in use).
   * @returns a Connection to close when done
   * @throws Error when the database cannot be reached or the login is refused
   */
  async getConnection(): Promise<Connection> {
    const handle = this.native.acquire();
    return new Connection(this.native, handle, false);
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
  const handle = native.acquire(); // (its own statement: a throw inside the return's arguments would leak native)
  return new Connection(native, handle, true);
}
