import { Injectable } from "@nestjs/common";
import { Pool } from "pg";
import { DB_HOST, DB_NAME, DB_PASSWORD, DB_POOL_SIZE, DB_PORT, DB_USER } from "../config";

// One connection pool for the application, injected where SQL runs (replaces TypeORM's connection).
@Injectable()
export class DatabaseService {
  readonly pool: Pool = new Pool({
    host: DB_HOST,
    port: DB_PORT,
    user: DB_USER,
    password: DB_PASSWORD,
    database: DB_NAME,
    max: DB_POOL_SIZE,
  });
}
