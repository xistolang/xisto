import { Injectable } from "@nestjs/common";
import { QueryResult } from "pg";
import { DatabaseService } from "../shared/database.service";
import { TagEntity } from "./tag.entity";

// Not cached: one trivial query on a small table is cheaper than the cache round trip (measured: docs/poc9-nest.md).
@Injectable()
export class TagService {
  constructor(private readonly db: DatabaseService) {}

  async findAll(): Promise<TagEntity[]> {
    const res: QueryResult<TagEntity> = await this.db.pool.query(`SELECT "id", "tag" FROM "tag"`, []);
    return res.rows;
  }
}
