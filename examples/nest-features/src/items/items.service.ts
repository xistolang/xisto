import { Inject, Injectable, Logger, NotFoundException, OnApplicationBootstrap, OnModuleInit, Optional } from "@nestjs/common";
import { ConfigService } from "@nestjs/config";
import { Cron } from "@nestjs/schedule";
import { CreateItemDto, Info, Item, ItemList } from "./item.dto";
import { MemoryStore } from "./store";

@Injectable()
export class ItemsService implements OnModuleInit, OnApplicationBootstrap {
  private readonly logger = new Logger(ItemsService.name);
  ticks: number = 0;
  started: boolean = false;

  constructor(
    @Inject("ITEMS_PREFIX") private readonly prefix: string,
    @Inject("STORE") private readonly store: MemoryStore,
    @Inject("ALIAS") private readonly alias: MemoryStore,
    @Optional() @Inject("MISSING") private readonly missing: string | null,
    private readonly config: ConfigService,
  ) {}

  onModuleInit(): void {
    this.logger.log("module init");
  }

  onApplicationBootstrap(): void {
    this.started = true;
  }

  @Cron("* * * * * *")
  tick(): void {
    this.ticks = this.ticks + 1;
  }

  list(q: string | null): ItemList {
    const items = this.store.items.filter((i: Item): boolean => q == null || i.name.includes(q));
    return { items: items, total: items.length };
  }

  get(id: number): Item {
    const found = this.store.items.find((i: Item): boolean => i.id === id);
    if (found == null) {
      throw new NotFoundException(`Item ${id} not found`);
    }
    return found;
  }

  create(dto: CreateItemDto): Item {
    const item: Item = { id: this.store.next, name: this.prefix + dto.name, qty: dto.qty ?? 1, kind: dto.kind };
    this.store.next = this.store.next + 1;
    this.store.items.push(item);
    return item;
  }

  remove(id: number): void {
    const at = this.store.items.findIndex((i: Item): boolean => i.id === id);
    if (at < 0) {
      throw new NotFoundException();
    }
    this.store.items.splice(at, 1);
  }

  info(): Info {
    return {
      app: this.config.get<string>("APP_NAME", "none") ?? "none",
      limit: this.config.get<string>("ITEMS_LIMIT") ?? "",
      prefix: this.prefix,
      missing: this.missing == null,
      alias: this.alias === this.store,
    };
  }
}
