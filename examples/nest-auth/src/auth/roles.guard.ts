import { ExecutionContext, Injectable } from "@nestjs/common";
import { Reflector } from "@nestjs/core";
import { Request } from "express";
import { ROLES_KEY } from "../common/public.decorator";
import { AuthUser } from "./user";

@Injectable()
export class RolesGuard {
  constructor(private readonly reflector: Reflector) {}

  canActivate(context: ExecutionContext): boolean {
    const roles = this.reflector.getAllAndOverride<string[]>(ROLES_KEY, [context.getHandler(), context.getClass()]);
    if (!roles) {
      return true;
    }
    const user = context.switchToHttp().getRequest<Request>().user as AuthUser;
    return roles.some((r: string): boolean => user.roles.includes(r));
  }
}
