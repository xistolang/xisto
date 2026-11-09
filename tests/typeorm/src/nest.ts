// @nestjs/typeorm: TypeOrmModule.forRoot / forFeature, @InjectRepository and a DataSource in a constructor, in a Nest
// application on PostgreSQL (tests/typeorm/run.sh starts it, asks its routes and stops it)
import { Controller, Get, Injectable, Module, Param } from "@nestjs/common";
import { NestFactory } from "@nestjs/core";
import { InjectRepository, TypeOrmModule } from "@nestjs/typeorm";
import { Column, DataSource, Entity, PrimaryColumn, Repository } from "typeorm";

@Entity("typeorm_nest_item")
class Item {
  @PrimaryColumn({ name: "id" })
  id: int;
  @Column({ name: "item_name" })
  name: string;
}

interface Count {
  n: string;
}

@Injectable()
class ItemService {
  constructor(@InjectRepository(Item) private readonly items: Repository<Item>, private readonly dataSource: DataSource) {}

  async setup(): Promise<void> {
    await this.dataSource.query("DROP TABLE IF EXISTS typeorm_nest_item");
    await this.dataSource.query("CREATE TABLE typeorm_nest_item (id int PRIMARY KEY, item_name text NOT NULL)");
    await this.items.insert({ id: 1, name: "one" });
    await this.items.insert({ id: 2, name: "two" });
  }

  async all(): Promise<Item[]> {
    return this.items.find({ order: { id: "ASC" } });
  }

  async one(id: int): Promise<Item | null> {
    return this.items.findOneBy({ id: id });
  }

  async teardown(): Promise<void> {
    await this.dataSource.query("DROP TABLE typeorm_nest_item");
  }

  async count(): Promise<string> {
    const rows: Count[] = await this.dataSource.query("SELECT COUNT(*)::text AS n FROM typeorm_nest_item");
    return rows[0].n;
  }
}

@Controller("items")
class ItemController {
  constructor(private readonly service: ItemService) {}

  @Get("setup")
  async setup(): Promise<string> {
    await this.service.setup();
    return "ready";
  }

  @Get()
  async all(): Promise<Item[]> {
    return this.service.all();
  }

  @Get("teardown")
  async teardown(): Promise<string> {
    await this.service.teardown();
    return "dropped";
  }

  @Get("count")
  async count(): Promise<string> {
    return this.service.count();
  }

  @Get(":id")
  async one(@Param("id") id: string): Promise<Item | null> {
    return this.service.one(Math.trunc(Number(id)));
  }
}

const port = Number(process.env.get("PGPORT_TEST") ?? "55432");

@Module({
  imports: [
    TypeOrmModule.forRoot({ type: "postgres", host: "127.0.0.1", port: Math.trunc(port), username: "xisto", password: "xisto", database: "conduit", entities: [Item] }),
    TypeOrmModule.forFeature([Item]),
  ],
  controllers: [ItemController],
  providers: [ItemService],
})
class AppModule {}

async function main(): Promise<void> {
  const app = NestFactory.create(AppModule, {});
  await app.listen(Math.trunc(Number(process.env.get("NEST_PORT") ?? "38411")));
}
main();
