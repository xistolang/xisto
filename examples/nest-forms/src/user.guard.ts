import { CanActivate, ExecutionContext, Injectable, UnauthorizedException } from "@nestjs/common";

// puts the caller on the request, as an authentication guard does: x-user is "sub" or "sub:tenant"
@Injectable()
export class UserGuard implements CanActivate {
  canActivate(context: ExecutionContext): boolean {
    const req = context.switchToHttp().getRequest();
    const header = req.header("x-user");
    if (!header) {
      throw new UnauthorizedException();
    }
    const parts = header.split(":");
    req.user = parts.length > 1 ? { sub: parts[0], tid: parts[1] } : { sub: parts[0] };
    return true;
  }
}
