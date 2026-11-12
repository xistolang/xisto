import { BadRequestException, Body, Controller, ForbiddenException, Get, HttpCode, Logger, Param, Post, Request, UseGuards } from "@nestjs/common";
import { ApiHeader } from "@nestjs/swagger";
import { Scoped } from "./decorators";
import { CreateTabletDto, LegacyDto, SortDto, UpdateKioskDto } from "./dtos";
import { FormsService } from "./forms.service";
import { UserGuard } from "./user.guard";

@Controller("forms")
export class FormsController {
  private readonly logger = new Logger(FormsController.name);

  constructor(private readonly forms: FormsService) {}

  @Post("tablet")
  @HttpCode(200)
  tablet(@Body() dto: CreateTabletDto) {
    return { ip: dto.ip, company: dto.company };
  }

  @Post("kiosk")
  @HttpCode(200)
  kiosk(@Body() dto: UpdateKioskDto) {
    return { name: dto.name ?? null, printer: dto.printer ?? null, ticket: dto.ticket ?? null };
  }

  @Post("sort")
  @HttpCode(200)
  sort(@Body() dto: SortDto) {
    return { sortOrder: dto.sortOrder ?? null, name: dto.name };
  }

  @Post("legacy")
  @HttpCode(200)
  legacy(@Body() dto: LegacyDto) {
    return { name: dto.name };
  }

  @Get("settings")
  settings() {
    return this.forms.settings();
  }

  @Get("me")
  @UseGuards(UserGuard)
  @ApiHeader({ name: "x-user", required: true, description: "The caller: sub or sub:tenant" })
  me(@Request() req: { user: { sub: string; tid?: string } }) {
    this.logger.warn({ event: "me", sub: req.user.sub, tid: req.user.tid ?? "none" });
    return { sub: req.user.sub, tenant: req.user.tid ?? "none" };
  }

  @Get("denied/:kind")
  denied(@Param("kind") kind: string) {
    if (kind === "object") {
      throw new ForbiddenException({ statusCode: 403, message: "menu hidden" });
    }
    if (kind === "list") {
      throw new BadRequestException(["first problem", "second problem"]);
    }
    throw new ForbiddenException();
  }

  @Get("item/:id")
  @UseGuards(UserGuard)
  item(@Request() req: { user?: { sub?: string } }, @Param("id") id: string) {
    this.logger.error({ event: "item", id, sub: req.user?.sub }, "item read");
    return { id, by: req.user?.sub ?? "nobody" };
  }
}

@Controller("local")
@Scoped("local")
export class LocalController {
  @Get()
  read() {
    return { scope: "local" };
  }
}
