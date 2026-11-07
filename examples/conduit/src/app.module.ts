import { Module } from "@nestjs/common";
import { AppController } from "./app.controller";
import { ArticleModule } from "./article/article.module";
import { ProfileModule } from "./profile/profile.module";
import { TagModule } from "./tag/tag.module";
import { UserModule } from "./user/user.module";

@Module({
  imports: [ArticleModule, UserModule, ProfileModule, TagModule],
  controllers: [AppController],
  providers: [],
})
export class ApplicationModule {}
