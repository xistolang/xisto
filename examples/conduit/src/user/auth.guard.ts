import { CanActivate, ExecutionContext, HttpException, HttpStatus, Injectable } from "@nestjs/common";
import { Request } from "express";
import { readToken } from "../shared/token";
import { UserService } from "./user.service";

// Was AuthMiddleware applied with consumer.forRoutes(...): same checks, as a guard on the routes.
@Injectable()
export class AuthGuard implements CanActivate {
  constructor(private readonly userService: UserService) {}

  async canActivate(context: ExecutionContext): Promise<boolean> {
    const req: Request = context.switchToHttp().getRequest();
    const auth = req.headers.authorization;
    if (!auth) {
      throw new HttpException("Not authorized.", HttpStatus.UNAUTHORIZED);
    }
    const payload = readToken(auth);
    if (payload === null) {
      throw new HttpException("Not authorized.", HttpStatus.UNAUTHORIZED);
    }
    await this.userService.findById(payload.id);
    return true;
  }
}
