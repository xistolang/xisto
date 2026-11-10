import { Body, Controller, Get, NotFoundException, Param, Post, UseInterceptors } from "@nestjs/common";
import { Throttle } from "@nestjs/throttler";
import { AuthUser } from "../auth/user";
import { StampInterceptor } from "../common/timing.interceptor";
import { CurrentUser } from "../common/current-user.decorator";
import { Raw } from "../common/public.decorator";

export class Item {
  id: number;
  name: string;
}
export class NewItem {
  name: string;
}

@Controller("items")
export class ItemsController {
  private readonly items: Item[] = [{ id: 1, name: "first" }];

  @Get()
  @UseInterceptors(StampInterceptor)
  list(): Item[] {
    return this.items;
  }

  @Get("me")
  me(@CurrentUser() user: AuthUser): AuthUser {
    return user;
  }

  @Raw()
  @Get("text")
  text(): string {
    return "plain text";
  }

  @Get("count")
  count(): number {
    return this.items.length;
  }

  @Throttle({ default: { limit: 2, ttl: 60000 } })
  @Get("limited")
  limited(): Item {
    return this.items[0];
  }

  @Get(":id")
  one(@Param("id") id: string): Item {
    const found = this.items.find((i: Item): boolean => String(i.id) === id);
    if (!found) {
      throw new NotFoundException(`Item ${id} not found`);
    }
    return found;
  }

  @Post()
  add(@Body() body: NewItem, @CurrentUser() user: AuthUser): Item {
    const item: Item = { id: this.items.length + 1, name: body.name + " by " + user.sub };
    this.items.push(item);
    return item;
  }
}
