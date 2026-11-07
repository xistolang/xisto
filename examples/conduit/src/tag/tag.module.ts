import { Module } from "@nestjs/common";
import { UserModule } from "../user/user.module";
import { TagController } from "./tag.controller";
import { TagService } from "./tag.service";

@Module({
  imports: [UserModule],
  providers: [TagService],
  controllers: [TagController],
})
export class TagModule {}
