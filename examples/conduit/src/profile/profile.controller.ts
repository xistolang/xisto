import { Controller, Delete, Get, Param, Post, UseGuards } from "@nestjs/common";
import { ApiTags } from "@nestjs/swagger";
import { AuthGuard } from "../user/auth.guard";
import { UserEmail, UserId } from "../user/user.decorator";
import { ProfileRO } from "./profile.interface";
import { ProfileService } from "./profile.service";

@ApiTags("profiles")
@Controller("profiles")
export class ProfileController {
  constructor(private readonly profileService: ProfileService) {}

  @Get(":username")
  async getProfile(@UserId() userId: int, @Param("username") username: string): Promise<ProfileRO | null> {
    return await this.profileService.findProfile(userId, username);
  }

  @UseGuards(AuthGuard)
  @Post(":username/follow")
  async follow(@UserEmail() email: string, @Param("username") username: string): Promise<ProfileRO> {
    return await this.profileService.follow(email, username);
  }

  @UseGuards(AuthGuard)
  @Delete(":username/follow")
  async unFollow(@UserId() userId: int, @Param("username") username: string): Promise<ProfileRO> {
    return await this.profileService.unFollow(userId, username);
  }
}
