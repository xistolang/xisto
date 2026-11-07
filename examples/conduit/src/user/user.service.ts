import { HttpException, HttpStatus, Injectable } from "@nestjs/common";
import { hash, verify } from "argon2";
import { isEmail } from "class-validator";
import { QueryResult } from "pg";
import { CacheService } from "../shared/cache.service";
import { DatabaseService } from "../shared/database.service";
import { generateJWT } from "../shared/token";
import { CreateUserDto } from "./dto/create-user.dto";
import { LoginUserDto } from "./dto/login-user.dto";
import { UpdateUserDto } from "./dto/update-user.dto";
import { UserEntity } from "./user.entity";
import { UserRO } from "./user.interface";

export type PublicUser = {
  id: int;
  username: string;
  email: string;
  bio: string;
  image: string;
};

export interface DeleteResult {
  raw: string[];
  affected: int;
}

const USER_COLUMNS = '"id", "username", "email", "bio", "image", "password"';

@Injectable()
export class UserService {
  constructor(
    private readonly db: DatabaseService,
    private readonly cache: CacheService,
  ) {}

  async findOne(dto: LoginUserDto): Promise<UserEntity | null> {
    const res: QueryResult<UserEntity> = await this.db.pool.query(`SELECT ${USER_COLUMNS} FROM "user" WHERE "email" = $1`, [dto.email]);
    if (res.rows.length === 0) {
      return null;
    }
    const user = res.rows[0];
    if (await verify(user.password, dto.password)) {
      return user;
    }
    return null;
  }

  async create(dto: CreateUserDto): Promise<UserRO> {
    // check uniqueness of username/email
    const taken: QueryResult<UserEntity> = await this.db.pool.query(
      `SELECT ${USER_COLUMNS} FROM "user" WHERE "username" = $1 OR "email" = $2 LIMIT 1`,
      [dto.username, dto.email],
    );
    if (taken.rows.length > 0) {
      throw new HttpException({ message: "Input data validation failed", errors: { username: "Username and email must be unique." } }, HttpStatus.BAD_REQUEST);
    }
    if (!isEmail(dto.email)) {
      throw new HttpException({ message: "Input data validation failed", _errors: { username: "Userinput is not valid." } }, HttpStatus.BAD_REQUEST);
    }
    const password = await hash(dto.password);
    const saved: QueryResult<UserEntity> = await this.db.pool.query(
      `INSERT INTO "user" ("username", "email", "password") VALUES ($1, $2, $3) RETURNING ${USER_COLUMNS}`,
      [dto.username, dto.email, password],
    );
    return this.buildUserRO(saved.rows[0]);
  }

  async update(id: int, dto: UpdateUserDto): Promise<PublicUser> {
    const res: QueryResult<PublicUser> = await this.db.pool.query(
      `UPDATE "user" SET "username" = COALESCE($2, "username"), "email" = COALESCE($3, "email"), "bio" = COALESCE($4, "bio"), "image" = COALESCE($5, "image")
       WHERE "id" = $1 RETURNING "id", "username", "email", "bio", "image"`,
      [id, dto.username, dto.email, dto.bio, dto.image],
    );
    await this.cache.invalidate(); // authors appear in the cached article lists
    return res.rows[0];
  }

  async delete(email: string): Promise<DeleteResult> {
    const res: QueryResult<UserEntity> = await this.db.pool.query(`DELETE FROM "user" WHERE "email" = $1`, [email]);
    await this.cache.invalidate();
    return { raw: [], affected: res.rowCount };
  }

  async findById(id: int): Promise<UserRO> {
    const res: QueryResult<UserEntity> = await this.db.pool.query(`SELECT ${USER_COLUMNS} FROM "user" WHERE "id" = $1`, [id]);
    if (res.rows.length === 0) {
      throw new HttpException({ errors: { User: " not found" } }, 401);
    }
    return this.buildUserRO(res.rows[0]);
  }

  async findByEmail(email: string): Promise<UserRO> {
    const res: QueryResult<UserEntity> = await this.db.pool.query(`SELECT ${USER_COLUMNS} FROM "user" WHERE "email" = $1`, [email]);
    return this.buildUserRO(res.rows[0]);
  }

  buildUserRO(user: UserEntity): UserRO {
    return {
      user: {
        id: user.id,
        username: user.username,
        email: user.email,
        bio: user.bio,
        token: generateJWT(user.id, user.username, user.email),
        image: user.image,
      },
    };
  }
}
