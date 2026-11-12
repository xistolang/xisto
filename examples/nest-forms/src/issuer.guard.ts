import { CanActivate, ExecutionContext, ForbiddenException, Inject, Injectable, Logger } from "@nestjs/common";
import { ConfigType } from "@nestjs/config";
import { Reflector } from "@nestjs/core";
import formsConfig from "./forms.config";

// lets a request through when its x-issuer header is the configured issuer (the base: its constructor is injected)
@Injectable()
export class IssuerGuard implements CanActivate {
  constructor(
    private readonly reflector: Reflector,
    @Inject(formsConfig.KEY)
    private readonly cfg: ConfigType<typeof formsConfig>,
  ) {}

  canActivate(context: ExecutionContext): boolean | Promise<boolean> {
    const req = context.switchToHttp().getRequest();
    if (req.header("x-issuer") !== this.cfg.issuer) {
      throw new ForbiddenException(`issuer ${this.cfg.issuer} required`);
    }
    return true;
  }
}

// the same check, with an open door for x-dev: 1; no constructor of its own (Nest builds it with IssuerGuard's)
@Injectable()
export class DevIssuerGuard extends IssuerGuard {
  private readonly devLogger = new Logger(DevIssuerGuard.name);

  override async canActivate(context: ExecutionContext): Promise<boolean> {
    const req = context.switchToHttp().getRequest();
    if (req.header("x-dev") === "1") {
      this.devLogger.log("dev door open");
      return true;
    }
    return super.canActivate(context);
  }
}
