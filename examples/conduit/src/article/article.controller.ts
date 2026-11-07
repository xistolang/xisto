import { Body, Controller, Delete, Get, Param, Post, Put, Query, UseGuards } from "@nestjs/common";
import { ApiTags } from "@nestjs/swagger";
import { AuthGuard } from "../user/auth.guard";
import { UserId } from "../user/user.decorator";
import { DeleteResult } from "../user/user.service";
import { ArticleData, ArticleRO, ArticlesRO, CommentsRO } from "./article.interface";
import { ArticleService } from "./article.service";
import { CreateArticleDto } from "./dto/create-article.dto";
import { CreateCommentDto } from "./dto/create-comment";
import { UpdateArticleDto } from "./dto/update-article.dto";

@ApiTags("articles")
@Controller("articles")
export class ArticleController {
  constructor(private readonly articleService: ArticleService) {}

  @Get()
  async findAll(
    @Query("tag") tag: string,
    @Query("author") author: string,
    @Query("favorited") favorited: string,
    @Query("limit") limit: string,
    @Query("offset") offset: string,
  ): Promise<ArticlesRO> {
    return await this.articleService.findAll(tag, author, favorited, limit, offset);
  }

  @UseGuards(AuthGuard)
  @Get("feed")
  async getFeed(@UserId() userId: int, @Query("limit") limit: string, @Query("offset") offset: string): Promise<ArticlesRO> {
    return await this.articleService.findFeed(userId, limit, offset);
  }

  @Get(":slug")
  async findOne(@Param("slug") slug: string): Promise<ArticleRO> {
    return await this.articleService.findOne(slug);
  }

  @Get(":slug/comments")
  async findComments(@Param("slug") slug: string): Promise<CommentsRO> {
    return await this.articleService.findComments(slug);
  }

  @UseGuards(AuthGuard)
  @Post()
  async create(@UserId() userId: int, @Body("article") articleData: CreateArticleDto): Promise<ArticleData> {
    return this.articleService.create(userId, articleData);
  }

  @UseGuards(AuthGuard)
  @Put(":slug")
  async update(@Param("slug") slug: string, @Body("article") articleData: UpdateArticleDto): Promise<ArticleRO> {
    return this.articleService.update(slug, articleData);
  }

  @UseGuards(AuthGuard)
  @Delete(":slug")
  async delete(@Param("slug") slug: string): Promise<DeleteResult> {
    return this.articleService.delete(slug);
  }

  @UseGuards(AuthGuard)
  @Post(":slug/comments")
  async createComment(@Param("slug") slug: string, @Body("comment") commentData: CreateCommentDto): Promise<ArticleRO> {
    return await this.articleService.addComment(slug, commentData);
  }

  @UseGuards(AuthGuard)
  @Delete(":slug/comments/:id")
  async deleteComment(@Param("slug") slug: string, @Param("id") id: string): Promise<ArticleRO> {
    return await this.articleService.deleteComment(slug, id);
  }

  @UseGuards(AuthGuard)
  @Post(":slug/favorite")
  async favorite(@UserId() userId: int, @Param("slug") slug: string): Promise<ArticleRO> {
    return await this.articleService.favorite(userId, slug);
  }

  @UseGuards(AuthGuard)
  @Delete(":slug/favorite")
  async unFavorite(@UserId() userId: int, @Param("slug") slug: string): Promise<ArticleRO> {
    return await this.articleService.unFavorite(userId, slug);
  }
}
