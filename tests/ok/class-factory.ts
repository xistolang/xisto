// class X extends F(args): a class returned by a function with arguments, as PassportStrategy(Strategy, "jwt") and
// AuthGuard("jwt") are written. xisto: X extends F, and F's static __mixin(args) runs as a decorator on X (after
// X's own decorators); a class F passed as a value needs a decorator so it has its ClassInfo (xisto:reflect).
import { ClassInfo } from "xisto:reflect";

function described(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {};
}

const names: Map<string, string> = new Map<string, string>();

@described()
class Strategy {
  kind(): string {
    return "jwt-strategy";
  }
}

class StrategyBase {
  options: string;
  constructor(options: string) {
    this.options = options;
  }
  static __mixin(strategy: ClassInfo, name: string): (c: ClassInfo) => void {
    return (c: ClassInfo): void => {
      names.set(c.name, `${name} via ${strategy.name}`);
    };
  }
  describe(): string {
    return `${this.options}`;
  }
}

@described()
class JwtStrategy extends StrategyBase(Strategy, "jwt") {
  constructor() {
    super("secret");
  }
  validate(sub: string): string {
    return "user " + sub;
  }
}

const s = new JwtStrategy();
console.log(s.describe(), s.validate("42"), names.get("JwtStrategy"));

class GuardBase {
  static __mixin(name: string): (c: ClassInfo) => void {
    return (c: ClassInfo): void => {
      names.set(c.name, name);
    };
  }
  canActivate(): boolean {
    return true;
  }
}
class AdminGuard extends GuardBase("admin") {
  override canActivate(): boolean {
    return names.get("AdminGuard") === "admin";
  }
}
console.log(new AdminGuard().canActivate(), names.size);
