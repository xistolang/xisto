import { Module } from "@nestjs/common";
import { ConfigModule } from "@nestjs/config";
import { DiscoveryModule } from "@nestjs/core";
import { ScheduleModule } from "@nestjs/schedule";
import { HealthController } from "./health.controller";
import { ItemsController } from "./items/items.controller";
import { ItemsService } from "./items/items.service";
import { MemoryStore } from "./items/store";
import { TeapotFilter } from "./items/teapot.filter";

@Module({
  imports: [ConfigModule.forRoot({ isGlobal: true, envFilePath: ".env.test" }), ScheduleModule.forRoot(), DiscoveryModule],
  controllers: [ItemsController, HealthController],
  providers: [
    ItemsService,
    TeapotFilter,
    { provide: "ITEMS_PREFIX", useValue: "item-" },
    { provide: "STORE", useClass: MemoryStore },
    { provide: "ALIAS", useExisting: "STORE" },
  ],
})
export class AppModule {}
