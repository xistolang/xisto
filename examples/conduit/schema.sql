-- Conduit (RealWorld) schema: the tables TypeORM creates from the entities of
-- lujakob/nestjs-realworld-example-app (synchronize: true), written out so the
-- Node and the Xisto builds of the port share one database.
CREATE TABLE IF NOT EXISTS "user" (
  "id" SERIAL PRIMARY KEY,
  "username" varchar NOT NULL,
  "email" varchar NOT NULL,
  "bio" varchar NOT NULL DEFAULT '',
  "image" varchar NOT NULL DEFAULT '',
  "password" varchar NOT NULL
);
CREATE TABLE IF NOT EXISTS "article" (
  "id" SERIAL PRIMARY KEY,
  "slug" varchar NOT NULL,
  "title" varchar NOT NULL,
  "description" varchar NOT NULL DEFAULT '',
  "body" varchar NOT NULL DEFAULT '',
  "created" timestamp NOT NULL DEFAULT now(),
  "updated" timestamp NOT NULL DEFAULT now(),
  "tagList" text NOT NULL,
  "favoriteCount" integer NOT NULL DEFAULT 0,
  "authorId" integer REFERENCES "user"("id") ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS "comment" (
  "id" SERIAL PRIMARY KEY,
  "body" varchar NOT NULL,
  "articleId" integer REFERENCES "article"("id") ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS "follows" (
  "id" SERIAL PRIMARY KEY,
  "followerId" integer NOT NULL,
  "followingId" integer NOT NULL
);
CREATE TABLE IF NOT EXISTS "tag" (
  "id" SERIAL PRIMARY KEY,
  "tag" varchar NOT NULL
);
CREATE TABLE IF NOT EXISTS "user_favorites_article" (
  "userId" integer NOT NULL REFERENCES "user"("id") ON DELETE CASCADE,
  "articleId" integer NOT NULL REFERENCES "article"("id") ON DELETE CASCADE,
  PRIMARY KEY ("userId", "articleId")
);
CREATE INDEX IF NOT EXISTS "article_slug" ON "article" ("slug");
CREATE INDEX IF NOT EXISTS "user_email" ON "user" ("email");
CREATE INDEX IF NOT EXISTS "user_username" ON "user" ("username");
