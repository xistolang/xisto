import { Controller, Get, UseGuards } from "@nestjs/common";
import { RolesGuard } from "../auth/roles.guard";
import { Roles } from "../common/public.decorator";

export class Report {
  total: number;
}

@Controller("admin")
@UseGuards(RolesGuard)
@Roles("admin")
export class AdminController {
  @Get("report")
  report(): Report {
    return { total: 42 };
  }
}
