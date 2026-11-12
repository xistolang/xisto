// @nestjs/passport for xisto: class JwtStrategy extends PassportStrategy(Strategy, "jwt") registers a strategy
// under its name (its constructor's options say how a token is checked, its validate() what the request's user
// is); class JwtAuthGuard extends AuthGuard("jwt") (or AuthGuard(["a", "b"]), tried in order) runs them: the
// first that passes puts validate()'s value on request.user, none passing answers 401 as Nest does.
// Node: the strategies are passport-jwt's (the only kind ported).
import { ClassInfo, MethodInfo } from "xisto:reflect";
import { DynamicModule, ExecutionContext, Injectable, Module, UnauthorizedException } from "@nestjs/common";
import { Request } from "express";
import { Observable } from "rxjs";
import { checkRequest, StrategyOptions } from "passport-jwt";

// name -> the strategy class; the instances the injector built (one per strategy class)
const strategyClasses: Map<string, ClassInfo> = new Map<string, ClassInfo>();
const strategies: PassportStrategy[] = [];
// AuthGuard("a") subclasses and the strategy names each runs
const guards: GuardNames[] = [];
const defaults: AuthModuleOptions = {};

class GuardNames {
  cls: ClassInfo;
  names: string[];
  constructor(cls: ClassInfo, names: string[]) {
    this.cls = cls;
    this.names = names;
  }
}

/** Options of PassportModule.register: the strategy AuthGuard() runs when it names none. */
export interface AuthModuleOptions {
  defaultStrategy?: string;
  session?: boolean;
  property?: string;
}

/** PassportModule.register({ defaultStrategy: "jwt" }). */
@Module({})
export class PassportModule {
  static register(options: AuthModuleOptions): DynamicModule {
    defaults.defaultStrategy = options.defaultStrategy;
    defaults.property = options.property;
    return { module: PassportModule, providers: [], exports: [] };
  }
}

/**
 * The base of a strategy: class JwtStrategy extends PassportStrategy(Strategy, "jwt") { constructor() { super(options) }
 * validate(payload) { return user } }.
 * @example class JwtStrategy extends PassportStrategy(Strategy, "jwt") {}
 */
@Injectable()
export class PassportStrategy {
  readonly options: StrategyOptions;
  constructor(options: StrategyOptions) {
    this.options = options;
    strategies.push(this);
  }
  // PassportStrategy(Strategy, name): the subclass is that strategy, under name ("jwt" by default)
  static __mixin(strategy: ClassInfo, name: string = ""): (c: ClassInfo) => void {
    return (c: ClassInfo): void => {
      strategyClasses.set(name.length > 0 ? name : "jwt", c);
    };
  }
}

/** What one strategy said: the user, an error validate() threw, or why it failed. */
class Outcome {
  user: unknown = null;
  error: Error | null = null;
  info: string = "";
}

function findMethod(c: ClassInfo, name: string): MethodInfo | null {
  let k: ClassInfo | null = c;
  while (k !== null) {
    const m = k.methods.find((x: MethodInfo): boolean => x.name === name);
    if (m !== null) {
      return m;
    }
    k = k.base;
  }
  return null;
}

// one strategy on the request: passport-jwt's check, then the strategy's validate()
function authenticate(name: string, req: Request): Outcome {
  const out = new Outcome();
  const c = strategyClasses.get(name);
  if (c === null) {
    throw new Error(`Unknown authentication strategy "${name}"`);
  }
  let instance: PassportStrategy | null = null;
  for (const s of strategies) {
    if (c.isInstance(s)) {
      instance = s;
    }
  }
  if (instance === null) {
    throw new Error(`Unknown authentication strategy "${name}"`);
  }
  const check = checkRequest(instance.options, req);
  if (!check.ok) {
    out.info = check.message;
    return out;
  }
  const validate = findMethod(c, "validate");
  if (validate === null) {
    out.user = null;
    out.info = "the strategy has no validate()";
    return out;
  }
  const args: unknown[] = [];
  if (instance.options.passReqToCallback ?? false) {
    args.push(req);
  }
  const p = validate.params[args.length];
  args.push(p.type.decode(check.payload));
  try {
    const user = validate.invoke(instance, args);
    if (user === null || (typeof user === "boolean" && !(user as boolean))) {
      return out;
    }
    out.user = user;
  } catch (e) {
    out.error = e;
  }
  return out;
}

/**
 * A guard that runs strategies: extends AuthGuard("jwt"), AuthGuard(["a", "b"]) (the first that passes) or
 * AuthGuard() (PassportModule's defaultStrategy). Override handleRequest to change what passes.
 * @example @UseGuards(JwtAuthGuard)
 */
@Injectable()
export class AuthGuard {
  // AuthGuard(type): the subclass runs those strategies
  static __mixin(type: unknown = null): (c: ClassInfo) => void {
    return (c: ClassInfo): void => {
      let names: string[] = [];
      if (typeof type === "string") {
        names = [type as string];
      } else if (type !== null) {
        names = type as string[];
      }
      guards.push(new GuardNames(c, names));
    };
  }

  /** Runs the strategies; the user goes on request.user (true), or handleRequest throws (401 by default).
   *  Typed as Nest's CanActivate (boolean | Promise<boolean> | Observable<boolean>) less the promise, which a function
   *  that is not async holds as the value it settles to: an override may declare any of the three. */
  canActivate(context: ExecutionContext): boolean | Observable<boolean> {
    const request = this.getRequest(context);
    let user: unknown = null;
    let err: Error | null = null;
    let info: unknown = null;
    for (const name of this.strategyNames()) {
      const o = authenticate(name, request);
      if (o.error !== null) {
        err = o.error;
        break;
      }
      if (o.user !== null) {
        user = o.user;
        break;
      }
      info = o.info;
    }
    request.user = this.handleRequest(err, user, info, context, null);
    return true;
  }

  /** The user when one passed; otherwise the strategy's error, or UnauthorizedException. */
  handleRequest(err: Error | null, user: unknown, info: unknown, context: ExecutionContext, status: unknown): unknown {
    if (err !== null) {
      throw err;
    }
    if (user === null) {
      throw new UnauthorizedException();
    }
    return user;
  }

  /** The request the strategies read. */
  getRequest(context: ExecutionContext): Request {
    return context.switchToHttp().getRequest<Request>();
  }

  private strategyNames(): string[] {
    for (const g of guards) {
      if (g.cls.isInstance(this)) {
        if (g.names.length > 0) {
          return g.names;
        }
      }
    }
    const d = defaults.defaultStrategy;
    if (d === null) {
      throw new Error('In order to use "defaultStrategy", please, ensure to import PassportModule in each place where AuthGuard() is being used. Otherwise, passport won\'t work correctly.');
    }
    return [d];
  }
}
