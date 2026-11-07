import { Injectable } from "@nestjs/common";
import { QueryResult } from "pg";
import slug from "slug";
import { CacheService } from "../shared/cache.service";
import { DatabaseService } from "../shared/database.service";
import { DeleteResult } from "../user/user.service";
import { ArticleData, ArticleListItem, ArticleRO, ArticlesRO, CommentData, CommentsRO } from "./article.interface";
import { CreateArticleDto } from "./dto/create-article.dto";
import { CreateCommentDto } from "./dto/create-comment";
import { UpdateArticleDto } from "./dto/update-article.dto";

// "tagList" is TypeORM's simple-array: the tags joined with commas
type ArticleRow = {
  id: int;
  slug: string;
  title: string;
  description: string;
  body: string;
  created: string;
  updated: string;
  tagList: string;
  favoriteCount: int;
};

type ArticleAuthorRow = {
  id: int;
  slug: string;
  title: string;
  description: string;
  body: string;
  created: string;
  updated: string;
  tagList: string;
  favoriteCount: int;
  authorId: int;
  authorUsername: string;
  authorEmail: string;
  authorBio: string;
  authorImage: string;
};

type IdRow = {
  id: int;
};

type CountRow = {
  count: int;
};

const ISO = `'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"'`;
const ARTICLE_COLUMNS = `a."id", a."slug", a."title", a."description", a."body", to_char(a."created", ${ISO}) AS "created",
  to_char(a."updated", ${ISO}) AS "updated", a."tagList", a."favoriteCount"`;

function tags(list: string): string[] {
  if (list.length === 0) {
    return [];
  }
  return list.split(",");
}

@Injectable()
export class ArticleService {
  constructor(
    private readonly db: DatabaseService,
    private readonly cache: CacheService,
  ) {}

  // the list and an article are the same for every user: cached (CacheService), invalidated by every write below
  async findAll(tag: string, author: string, favorited: string, limit: string, offset: string): Promise<ArticlesRO> {
    const c = await this.cache.lookup(`articles:${tag || ""}|${author || ""}|${favorited || ""}|${limit || ""}|${offset || ""}`);
    let hit: string | null = null;
    if (c !== null) {
      hit = c.value;
    }
    if (hit !== null) {
      const cached: ArticlesRO = JSON.parse(hit);
      return cached;
    }
    const fresh = await this.listFromDb(tag, author, favorited, limit, offset);
    await this.cache.store(c, JSON.stringify(fresh));
    return fresh;
  }

  async findOne(articleSlug: string): Promise<ArticleRO> {
    const c = await this.cache.lookup(`article:${articleSlug}`);
    let hit: string | null = null;
    if (c !== null) {
      hit = c.value;
    }
    if (hit !== null) {
      const cached: ArticleRO = JSON.parse(hit);
      return cached;
    }
    const fresh = await this.oneFromDb(articleSlug);
    await this.cache.store(c, JSON.stringify(fresh));
    return fresh;
  }

  private async listFromDb(tag: string, author: string, favorited: string, limit: string, offset: string): Promise<ArticlesRO> {
    const where: string[] = ["1 = 1"];
    const params: string[] = [];
    if (tag) {
      params.push(`%${tag}%`);
      where.push(`a."tagList" LIKE $${params.length}`);
    }
    if (author) {
      params.push(author);
      where.push(`u."username" = $${params.length}`);
    }
    if (favorited) {
      params.push(favorited);
      where.push(`a."id" IN (SELECT f."articleId" FROM "user_favorites_article" f JOIN "user" fu ON fu."id" = f."userId" WHERE fu."username" = $${params.length})`);
    }
    const from = `FROM "article" a LEFT JOIN "user" u ON u."id" = a."authorId" WHERE ${where.join(" AND ")}`;
    const count: QueryResult<CountRow> = await this.db.pool.query(`SELECT COUNT(*)::int AS "count" ${from}`, params);
    let page = "";
    if (limit) {
      params.push(limit);
      page = `${page} LIMIT $${params.length}::int`;
    }
    if (offset) {
      params.push(offset);
      page = `${page} OFFSET $${params.length}::int`;
    }
    const res: QueryResult<ArticleAuthorRow> = await this.db.pool.query(
      `SELECT ${ARTICLE_COLUMNS}, u."id" AS "authorId", u."username" AS "authorUsername", u."email" AS "authorEmail",
         u."bio" AS "authorBio", u."image" AS "authorImage" ${from} ORDER BY a."created" DESC${page}`,
      params,
    );
    const articles: ArticleListItem[] = res.rows.map((r: ArticleAuthorRow): ArticleListItem => {
      return {
        id: r.id, slug: r.slug, title: r.title, description: r.description, body: r.body, created: r.created, updated: r.updated,
        tagList: tags(r.tagList), favoriteCount: r.favoriteCount,
        author: { id: r.authorId, username: r.authorUsername, email: r.authorEmail, bio: r.authorBio, image: r.authorImage },
      };
    });
    return { articles: articles, articlesCount: count.rows[0].count };
  }

  async findFeed(userId: int, limit: string, offset: string): Promise<ArticlesRO> {
    const follows: QueryResult<IdRow> = await this.db.pool.query(`SELECT "followingId" AS "id" FROM "follows" WHERE "followerId" = $1`, [userId]);
    if (follows.rows.length === 0) {
      return { articles: [], articlesCount: 0 };
    }
    const from = `FROM "article" a WHERE a."authorId" IN (SELECT "followingId" FROM "follows" WHERE "followerId" = $1)`;
    const params: string[] = [`${userId}`];
    const count: QueryResult<CountRow> = await this.db.pool.query(`SELECT COUNT(*)::int AS "count" ${from}`, params);
    let page = "";
    if (limit) {
      params.push(limit);
      page = `${page} LIMIT $${params.length}::int`;
    }
    if (offset) {
      params.push(offset);
      page = `${page} OFFSET $${params.length}::int`;
    }
    const res: QueryResult<ArticleRow> = await this.db.pool.query(`SELECT ${ARTICLE_COLUMNS} ${from} ORDER BY a."created" DESC${page}`, params);
    const articles: ArticleListItem[] = res.rows.map((r: ArticleRow): ArticleListItem => {
      return {
        id: r.id, slug: r.slug, title: r.title, description: r.description, body: r.body, created: r.created, updated: r.updated,
        tagList: tags(r.tagList), favoriteCount: r.favoriteCount,
      };
    });
    return { articles: articles, articlesCount: count.rows[0].count };
  }

  // the article with its comments, as TypeORM loaded it (eager relation); null when the slug is unknown
  async load(articleSlug: string): Promise<ArticleData | null> {
    const res: QueryResult<ArticleRow> = await this.db.pool.query(`SELECT ${ARTICLE_COLUMNS} FROM "article" a WHERE a."slug" = $1`, [articleSlug]);
    if (res.rows.length === 0) {
      return null;
    }
    const r = res.rows[0];
    const comments: QueryResult<CommentData> = await this.db.pool.query(`SELECT "id", "body" FROM "comment" WHERE "articleId" = $1 ORDER BY "id"`, [r.id]);
    return {
      id: r.id, slug: r.slug, title: r.title, description: r.description, body: r.body, created: r.created, updated: r.updated,
      tagList: tags(r.tagList), favoriteCount: r.favoriteCount, comments: comments.rows,
    };
  }

  private async oneFromDb(articleSlug: string): Promise<ArticleRO> {
    const article = await this.load(articleSlug);
    if (article === null) {
      return {};
    }
    return { article: article };
  }

  async addComment(articleSlug: string, commentData: CreateCommentDto): Promise<ArticleRO> {
    await this.db.pool.query(`INSERT INTO "comment" ("body", "articleId") SELECT $1, "id" FROM "article" WHERE "slug" = $2`, [commentData.body, articleSlug]);
    await this.cache.invalidate();
    return this.findOne(articleSlug);
  }

  async deleteComment(articleSlug: string, id: string): Promise<ArticleRO> {
    await this.db.pool.query(
      `DELETE FROM "comment" WHERE "id" = $1::int AND "articleId" = (SELECT "id" FROM "article" WHERE "slug" = $2)`,
      [id, articleSlug],
    );
    await this.cache.invalidate();
    return this.findOne(articleSlug);
  }

  async favorite(id: int, articleSlug: string): Promise<ArticleRO> {
    const added: QueryResult<IdRow> = await this.db.pool.query(
      `INSERT INTO "user_favorites_article" ("userId", "articleId") SELECT $1, "id" FROM "article" WHERE "slug" = $2
       ON CONFLICT DO NOTHING RETURNING "articleId" AS "id"`,
      [id, articleSlug],
    );
    if (added.rows.length > 0) {
      await this.db.pool.query(`UPDATE "article" SET "favoriteCount" = "favoriteCount" + 1, "updated" = now() WHERE "id" = $1`, [added.rows[0].id]);
      await this.cache.invalidate();
    }
    return this.findOne(articleSlug);
  }

  async unFavorite(id: int, articleSlug: string): Promise<ArticleRO> {
    const removed: QueryResult<IdRow> = await this.db.pool.query(
      `DELETE FROM "user_favorites_article" WHERE "userId" = $1 AND "articleId" = (SELECT "id" FROM "article" WHERE "slug" = $2) RETURNING "articleId" AS "id"`,
      [id, articleSlug],
    );
    if (removed.rows.length > 0) {
      await this.db.pool.query(`UPDATE "article" SET "favoriteCount" = "favoriteCount" - 1, "updated" = now() WHERE "id" = $1`, [removed.rows[0].id]);
      await this.cache.invalidate();
    }
    return this.findOne(articleSlug);
  }

  async findComments(articleSlug: string): Promise<CommentsRO> {
    const article = await this.load(articleSlug);
    if (article === null) {
      return { comments: [] };
    }
    return { comments: article.comments };
  }

  async create(userId: int, articleData: CreateArticleDto): Promise<ArticleData> {
    const tagList: string[] = articleData.tagList || [];
    // as the original: the body is not taken from the request (it keeps the column default)
    const res: QueryResult<IdRow> = await this.db.pool.query(
      `INSERT INTO "article" ("slug", "title", "description", "tagList", "authorId") VALUES ($1, $2, $3, $4, $5) RETURNING "id"`,
      [this.slugify(articleData.title), articleData.title, articleData.description, tagList.join(","), userId],
    );
    await this.cache.invalidate();
    const saved: QueryResult<ArticleRow> = await this.db.pool.query(`SELECT ${ARTICLE_COLUMNS} FROM "article" a WHERE a."id" = $1`, [res.rows[0].id]);
    const r = saved.rows[0];
    return {
      id: r.id, slug: r.slug, title: r.title, description: r.description, body: r.body, created: r.created, updated: r.updated,
      tagList: tags(r.tagList), favoriteCount: r.favoriteCount, comments: [],
    };
  }

  async update(articleSlug: string, articleData: UpdateArticleDto): Promise<ArticleRO> {
    let tagList: string | null = null;
    const newTags = articleData.tagList;
    if (newTags) {
      tagList = newTags.join(",");
    }
    await this.db.pool.query(
      `UPDATE "article" SET "title" = COALESCE($2, "title"), "description" = COALESCE($3, "description"), "body" = COALESCE($4, "body"),
       "tagList" = COALESCE($5, "tagList"), "updated" = now() WHERE "slug" = $1`,
      [articleSlug, articleData.title, articleData.description, articleData.body, tagList],
    );
    await this.cache.invalidate();
    return this.findOne(articleSlug);
  }

  async delete(articleSlug: string): Promise<DeleteResult> {
    const res: QueryResult<IdRow> = await this.db.pool.query(`DELETE FROM "article" WHERE "slug" = $1`, [articleSlug]);
    await this.cache.invalidate();
    return { raw: [], affected: res.rowCount };
  }

  slugify(title: string): string {
    return slug(title, { lower: true }) + "-" + Math.floor(Math.random() * Math.pow(36, 6)).toString(36);
  }
}
