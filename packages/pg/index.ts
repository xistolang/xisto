// node-postgres: Pool and query(text, values), protocol 3.0 with SCRAM-SHA-256 (native/pg.c).
// A result is an object (JSON) that a typed declaration decodes: const r: QueryResult<Row> = await pool.query(...)
export interface PoolConfig {
  host?: string;
  port?: int;
  user?: string;
  password?: string;
  database?: string;
  max?: int;
}

export interface QueryResult<T> {
  rows: T[];
  rowCount: int;
}

declare class NativePool {
  constructor(config: object);
  query(text: string, values: unknown[]): object;
}

export class Pool {
  private native: NativePool;

  constructor(config: PoolConfig) {
    this.native = new NativePool(config);
  }

  async query(text: string, values: unknown[]): Promise<object> {
    return this.native.query(text, values);
  }
}
