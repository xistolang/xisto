import { Controller, Get } from "@nestjs/common";
import { SkipThrottle } from "@nestjs/throttler";
import { AuditLog } from "../common/audit-log";
import { Public, Raw } from "../common/public.decorator";

export class Health {
  status: string;
}

@Public()
@SkipThrottle()
@Controller()
export class HealthController {
  constructor(private readonly audit: AuditLog) {}

  @Get("health")
  check(): Health {
    return { status: "ok" };
  }

  @Raw()
  @Get("audit")
  lines(): string[] {
    return this.audit.lines;
  }
}
