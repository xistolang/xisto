import { Module } from "@nestjs/common";
import { UserModule } from "../user/user.module";
import { ArticleController } from "./article.controller";
import { ArticleService } from "./article.service";

@Module({
  imports: [UserModule],
  providers: [ArticleService],
  controllers: [ArticleController],
})
export class ArticleModule {}
