import { Body, Controller, Delete, Get, HttpException, Param, Post, Put, UseGuards, UsePipes, ValidationPipe } from "@nestjs/common";
import { ApiTags } from "@nestjs/swagger";
import { AuthGuard } from "./auth.guard";
import { CreateUserDto } from "./dto/create-user.dto";
import { LoginUserDto } from "./dto/login-user.dto";
import { UpdateUserDto } from "./dto/update-user.dto";
import { UserEmail, UserId } from "./user.decorator";
import { LoginRO, UserRO } from "./user.interface";
import { DeleteResult, PublicUser, UserService } from "./user.service";
import { generateJWT } from "../shared/token";

@ApiTags("user")
@Controller()
export class UserController {
  constructor(private readonly userService: UserService) {}

  @UseGuards(AuthGuard)
  @Get("user")
  async findMe(@UserEmail() email: string): Promise<UserRO> {
    return await this.userService.findByEmail(email);
  }

  @UseGuards(AuthGuard)
  @Put("user")
  async update(@UserId() userId: int, @Body("user") userData: UpdateUserDto): Promise<PublicUser> {
    return await this.userService.update(userId, userData);
  }

  @UsePipes(new ValidationPipe())
  @Post("users")
  async create(@Body("user") userData: CreateUserDto): Promise<UserRO> {
    return this.userService.create(userData);
  }

  @Delete("users/:slug")
  async delete(@Param("slug") slug: string): Promise<DeleteResult> {
    return await this.userService.delete(slug);
  }

  @UsePipes(new ValidationPipe())
  @Post("users/login")
  async login(@Body("user") loginUserDto: LoginUserDto): Promise<LoginRO> {
    const user = await this.userService.findOne(loginUserDto);
    if (user === null) {
      throw new HttpException({ errors: { User: " not found" } }, 401);
    }
    const token = generateJWT(user.id, user.username, user.email);
    return { user: { email: user.email, token: token, username: user.username, bio: user.bio, image: user.image } };
  }
}
