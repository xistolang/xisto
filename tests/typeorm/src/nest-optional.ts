// @Optional() @Inject(SYMBOL) with no provider: the constructor gets null (the reflected construct accepted only the written type)
import { Inject, Injectable, Module, Optional } from "@nestjs/common";
import { NestFactory } from "@nestjs/core";

export const RESOLVER = Symbol("RESOLVER");
export interface Resolver {
  resolve(sub: string): string;
}

@Injectable()
class Settings {
  name(): string {
    return "settings";
  }
}

@Injectable()
class Strategy {
  constructor(
    private readonly settings: Settings,
    @Optional() @Inject(RESOLVER) private readonly resolver?: Resolver,
  ) {}
  describe(): string {
    return `${this.settings.name()} resolver=${this.resolver ? "yes" : "none"}`;
  }
}

// @Optional() with a default (now: () => number = Date.now as an app writes it): no provider, so the default applies
export const CLOCK = Symbol("CLOCK");
@Injectable()
class Stamp {
  constructor(
    private readonly settings: Settings,
    @Optional() @Inject(CLOCK) private readonly now: () => number = (): number => 42,
  ) {}
  at(): string {
    return `${this.settings.name()} at ${this.now()}`;
  }
}

@Module({ providers: [Settings, Strategy, Stamp] })
class AppModule {}

const app = NestFactory.create(AppModule);
app.init();
console.log((app.get(Strategy) as Strategy).describe());
console.log((app.get(Stamp) as Stamp).at());
