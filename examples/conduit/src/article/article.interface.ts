import { PublicUser } from "../user/user.service";

export type CommentData = {
  id: int;
  body: string;
};

// An article as the original returned it from findOne: the entity with its eager comments.
export interface ArticleData {
  id: int;
  slug: string;
  title: string;
  description: string;
  body: string;
  created: string;
  updated: string;
  tagList: string[];
  favoriteCount: int;
  comments: CommentData[];
}

// In lists (query builder): no comments; the author when it was joined.
export interface ArticleListItem {
  id: int;
  slug: string;
  title: string;
  description: string;
  body: string;
  created: string;
  updated: string;
  tagList: string[];
  favoriteCount: int;
  author?: PublicUser;
}

export interface ArticleRO {
  article?: ArticleData;
}

export interface ArticlesRO {
  articles: ArticleListItem[];
  articlesCount: int;
}

export interface CommentsRO {
  comments: CommentData[];
}
