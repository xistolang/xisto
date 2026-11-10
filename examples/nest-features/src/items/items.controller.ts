import { Body, Controller, Delete, Get, HttpCode, HttpStatus, Param, ParseIntPipe, Post, Query, Res, UploadedFile, UseFilters, UseInterceptors, Version, BadRequestException } from "@nestjs/common";
import { DiscoveryService } from "@nestjs/core";
import { FileInterceptor, UploadedFileData } from "@nestjs/platform-express";
import { ApiOperation, ApiResponse, ApiTags } from "@nestjs/swagger";
import { Response } from "express";
import { CreateItemDto, Info, Item, ItemList } from "./item.dto";
import { ItemsService } from "./items.service";
import { TeapotFilter } from "./teapot.filter";

export class UploadInfo {
  field: string;
  name: string;
  type: string;
  size: number;
  text: string;
}

export class Ticks {
  ticking: boolean;
  started: boolean;
}

export class Names {
  service: boolean;
  controller: boolean;
}

@ApiTags("items")
@Controller("items")
export class ItemsController {
  constructor(
    private readonly items: ItemsService,
    private readonly discovery: DiscoveryService,
  ) {}

  @Version("1")
  @Get()
  @ApiOperation({ summary: "list the items" })
  list(@Query("q") q: string): ItemList {
    return this.items.list(q === "" ? null : q);
  }

  @Version("1")
  @Get("info")
  info(): Info {
    return this.items.info();
  }

  @Version("1")
  @Get("ticks")
  ticks(): Ticks {
    return { ticking: this.items.ticks > 0, started: this.items.started };
  }

  @Version("1")
  @Get("names")
  names(): Names {
    const p = this.discovery.getProviders().map((w) => w.name);
    const c = this.discovery.getControllers().map((w) => w.name);
    return { service: p.includes("ItemsService"), controller: c.includes("ItemsController") };
  }

  @Version("1")
  @Get("teapot")
  @UseFilters(TeapotFilter)
  teapot(): string {
    throw new BadRequestException("filtered");
  }

  @Version("1")
  @Get("boom")
  boom(): string {
    throw new Error("kaboom");
  }

  @Version("1")
  @Get(":id")
  @HttpCode(HttpStatus.OK)
  @ApiResponse({ status: 404, description: "no such item" })
  get(@Param("id", ParseIntPipe) id: number): Item {
    return this.items.get(id);
  }

  @Version("1")
  @Post()
  create(@Body() dto: CreateItemDto): Item {
    return this.items.create(dto);
  }

  @Version("1")
  @Delete(":id")
  @HttpCode(HttpStatus.NO_CONTENT)
  remove(@Param("id", ParseIntPipe) id: number): void {
    this.items.remove(id);
  }

  @Version("2")
  @Get("export/csv")
  exportCsv(@Res() res: Response): void {
    res.setHeader("Content-Type", "text/csv; charset=utf-8");
    res.setHeader("Content-Disposition", "attachment; filename=\"items.csv\"");
    res.status(200).send("id,name\n1,first\n");
  }

  @Version("1")
  @Post("upload")
  @HttpCode(200)
  @UseInterceptors(FileInterceptor("file"))
  upload(@UploadedFile() file: UploadedFileData): UploadInfo {
    return { field: file.fieldname, name: file.originalname, type: file.mimetype, size: file.size, text: new TextDecoder().decode(file.buffer) };
  }
}
