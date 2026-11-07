import { HttpException, HttpStatus, Injectable } from "@nestjs/common";
import { QueryResult } from "pg";
import { DatabaseService } from "../shared/database.service";
import { UserEntity } from "../user/user.entity";
import { ProfileData, ProfileRO } from "./profile.interface";

type FollowRow = {
  id: int;
};

const USER_COLUMNS = '"id", "username", "email", "bio", "image", "password"';

@Injectable()
export class ProfileService {
  constructor(private readonly db: DatabaseService) {}

  async findProfile(id: int, followingUsername: string): Promise<ProfileRO | null> {
    const found: QueryResult<UserEntity> = await this.db.pool.query(`SELECT ${USER_COLUMNS} FROM "user" WHERE "username" = $1`, [followingUsername]);
    if (found.rows.length === 0) {
      return null;
    }
    const user = found.rows[0];
    const profile: ProfileData = { username: user.username, bio: user.bio, image: user.image };
    const follows: QueryResult<FollowRow> = await this.db.pool.query(
      `SELECT "id" FROM "follows" WHERE "followerId" = $1 AND "followingId" = $2`,
      [id, user.id],
    );
    if (id !== 0) {
      profile.following = follows.rows.length > 0;
    }
    return { profile: profile };
  }

  async follow(followerEmail: string, username: string): Promise<ProfileRO> {
    if (!followerEmail || !username) {
      throw new HttpException("Follower email and username not provided.", HttpStatus.BAD_REQUEST);
    }
    const following: QueryResult<UserEntity> = await this.db.pool.query(`SELECT ${USER_COLUMNS} FROM "user" WHERE "username" = $1`, [username]);
    const follower: QueryResult<UserEntity> = await this.db.pool.query(`SELECT ${USER_COLUMNS} FROM "user" WHERE "email" = $1`, [followerEmail]);
    const followingUser = following.rows[0];
    const followerUser = follower.rows[0];
    if (followingUser.email === followerEmail) {
      throw new HttpException("FollowerEmail and FollowingId cannot be equal.", HttpStatus.BAD_REQUEST);
    }
    const existing: QueryResult<FollowRow> = await this.db.pool.query(
      `SELECT "id" FROM "follows" WHERE "followerId" = $1 AND "followingId" = $2`,
      [followerUser.id, followingUser.id],
    );
    if (existing.rows.length === 0) {
      await this.db.pool.query(`INSERT INTO "follows" ("followerId", "followingId") VALUES ($1, $2)`, [followerUser.id, followingUser.id]);
    }
    return { profile: { username: followingUser.username, bio: followingUser.bio, image: followingUser.image, following: true } };
  }

  async unFollow(followerId: int, username: string): Promise<ProfileRO> {
    if (followerId === 0 || !username) {
      throw new HttpException("FollowerId and username not provided.", HttpStatus.BAD_REQUEST);
    }
    const following: QueryResult<UserEntity> = await this.db.pool.query(`SELECT ${USER_COLUMNS} FROM "user" WHERE "username" = $1`, [username]);
    const followingUser = following.rows[0];
    if (followingUser.id === followerId) {
      throw new HttpException("FollowerId and FollowingId cannot be equal.", HttpStatus.BAD_REQUEST);
    }
    await this.db.pool.query(`DELETE FROM "follows" WHERE "followerId" = $1 AND "followingId" = $2`, [followerId, followingUser.id]);
    return { profile: { username: followingUser.username, bio: followingUser.bio, image: followingUser.image, following: false } };
  }
}
