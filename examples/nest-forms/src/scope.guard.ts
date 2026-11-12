import { CanActivate, ExecutionContext, ForbiddenException, Injectable } from "@nestjs/common";
import { Reflector } from "@nestjs/core";

// lets a request through when its x-scope header names the resource the controller declared
@Injectable()
export class ScopeGuard implements CanActivate {
  constructor(private readonly reflector: Reflector) {}

  canActivate(context: ExecutionContext): boolean {
    const resource = this.reflector.get<string>("resource", context.getClass());
    const req = context.switchToHttp().getRequest();
    const scope = req.header("x-scope");
    if (scope !== resource) {
      throw new ForbiddenException(`scope ${resource} required`);
    }
    return true;
  }
}
