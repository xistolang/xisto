import { Module } from "@nestjs/common";
import { CacheService } from "../shared/cache.service";
import { DatabaseService } from "../shared/database.service";
import { AuthGuard } from "./auth.guard";
import { UserController } from "./user.controller";
import { UserService } from "./user.service";

@Module({
  providers: [DatabaseService, CacheService, UserService, AuthGuard],
  controllers: [UserController],
  exports: [UserService, AuthGuard, DatabaseService, CacheService],
})
export class UserModule {}
