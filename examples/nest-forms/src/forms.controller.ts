import { BadRequestException, Body, Controller, ForbiddenException, Get, HttpCode, Logger, Param, Post, Query, Req, Request, Res, UseGuards } from "@nestjs/common";
import { Request as ExpressRequest, Response as ExpressResponse } from "express";
import { ApiHeader } from "@nestjs/swagger";
import { Scoped } from "./decorators";
import { CreateTabletDto, LegacyDto, NoQueryPipe, SearchQueryDto, SortDto, UpdateKioskDto } from "./dtos";
import { CurrentUser } from "./user.decorator";
import { FormsService } from "./forms.service";
import { UserGuard } from "./user.guard";
import { DevIssuerGuard } from "./issuer.guard";

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

  @Get("issued")
  @UseGuards(DevIssuerGuard)
  issued() {
    return { issued: true };
  }

  @Get(["url", "url-alias"])
  url(@Req() req: ExpressRequest) {
    return { originalUrl: req.originalUrl, path: req.path };
  }

  @Post("stream")
  @HttpCode(200)
  async stream(@Req() req: ExpressRequest) {
    const chunks: Buffer[] = [];
    let size = 0;
    for await (const chunk of req) {
      const buffer = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk as string);
      size += buffer.byteLength;
      chunks.push(buffer);
    }
    return { size, text: Buffer.concat(chunks).toString("utf8") };
  }

  @Get("bytes")
  bytes(@Res() res: ExpressResponse): void {
    res.status(200);
    res.setHeader("content-type", "text/plain; charset=utf-8");
    res.end(Buffer.from("olá, bytes"));
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

  @Get("search")
  search(@Query() q: SearchQueryDto) {
    return { term: q.term, page: q.page ?? 1, sortOrder: q.sortOrder ?? "asc" };
  }

  @Get("plain")
  plain(@Query(NoQueryPipe) _q: unknown) {
    return { plain: true };
  }

  @Get("whoami")
  @UseGuards(UserGuard)
  whoami(@CurrentUser("sub") sub: string, @CurrentUser() user: { sub: string; tid?: string }) {
    return { sub, tenant: user.tid ?? "none" };
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
