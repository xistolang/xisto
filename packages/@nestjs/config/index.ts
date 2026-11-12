// @nestjs/config for xisto: ConfigModule.forRoot() reads a .env file into process.env (without overriding what
// the environment already has, as dotenv does) and provides ConfigService, which reads process.env.
import { readFileSync, existsSync } from "xisto:fs";
import { DynamicModule, Injectable, Module, FactoryProvider } from "@nestjs/common";

/**
 * A namespace of configuration: registerAs("database", () => ({ ... })) gives one, which ConfigModule.forRoot
 * loads ({ load: [databaseConfig] }) or ConfigModule.forFeature provides; then config.get("database.host") reads
 * it, and @Inject(databaseConfig.KEY) private cfg: ConfigType<typeof databaseConfig> receives the object.
 * Compatibility: Nest's registerAs returns the factory function itself (with a KEY); here it is this object,
 * which forRoot's load, forFeature and get() use the same way.
 */
export class ConfigFactory<T = any> {
  KEY: string;
  namespace: string;
  /** (the factory's type, which ConfigType<typeof f> names; never set) */
  __type: T | null = null;
  constructor(namespace: string) {
    this.KEY = `CONFIGURATION(${namespace})`;
    this.namespace = namespace;
  }
}

// each namespace's factory (a ConfigFactory<T> of any T is read through its namespace)
class FactoryFn {
  fn: () => any;
  constructor(fn: () => any) {
    this.fn = fn;
  }
}
const factories: Map<string, FactoryFn> = new Map<string, FactoryFn>();

function namespaceOf(f: unknown): string {
  const x: any = f;
  return x.namespace as string;
}

function build(f: unknown): any {
  const ns = namespaceOf(f);
  const fn = factories.get(ns);
  if (fn === null) {
    throw new Error(`Configuration namespace "${ns}" was not registered`);
  }
  const value: any = fn.fn();
  loaded[ns] = value;
  return value;
}

/** The object a configuration namespace gives: ConfigType<typeof databaseConfig>. */
export type ConfigType<F> = NonNullable<F["__type"]>;

/** A configuration namespace from a factory, read when ConfigModule.forRoot (or forFeature) loads it. */
export function registerAs<T>(token: string, factory: () => T): ConfigFactory<T> {
  factories.set(token, new FactoryFn((): any => factory() as any));
  return new ConfigFactory<T>(token);
}

/** Options of ConfigModule.forRoot. envFilePath: a file, or a list (the first that exists wins each key). */
export class ConfigModuleOptions {
  isGlobal?: boolean;
  envFilePath?: string | string[];
  ignoreEnvFile?: boolean;
  cache?: boolean;
  expandVariables?: boolean;
  load?: unknown[]; // registerAs's factories
}

/** The namespaces forRoot loaded, by name (their factories' values). */
const loaded: Record<string, any> = {};

/** a.b.c in the loaded namespaces: null when a part is not there (as lodash get gives undefined) */
function lookupPath(path: string): any {
  const parts = path.split(".");
  if (!(parts[0] in loaded)) {
    return null;
  }
  let cur: any = loaded[parts[0]];
  for (let i = 1; i < parts.length; i++) {
    if (cur == null || typeof cur !== "object") {
      return null;
    }
    if (!(parts[i] in cur)) {
      return null;
    }
    cur = cur[parts[i]];
  }
  return cur;
}

/**
 * Reads configuration from process.env.
 * Compatibility: values are the environment's strings; get<number>("PORT") gives the string as Node does, so
 * convert it yourself: Number(config.get("PORT")).
 * @example const host = this.config.get<string>("DB_HOST", "localhost")
 */
@Injectable()
export class ConfigService {
  /** The value of `key`, or `defaultValue` (null when not given) when it is not set. */
  get<T = string>(key: string, defaultValue: T | null = null): T | null {
    const v = process.env.get(key);
    if (v !== null) {
      return v as unknown as T;
    }
    const found: any = lookupPath(key); /* a loaded namespace: "database", "app.port" */
    if (found == null) {
      return defaultValue;
    }
    return found as T;
  }

  /** The value of `key`; throws when it is not set. */
  getOrThrow<T = string>(key: string): T {
    const v = process.env.get(key);
    if (v !== null) {
      return v as unknown as T;
    }
    const found: any = lookupPath(key);
    if (found == null) {
      throw new TypeError(`Configuration key "${key}" does not exist`);
    }
    return found as T;
  }
}

/** Parses a .env text: KEY=value lines, # comments, quotes removed. */
export function parseEnv(text: string): Map<string, string> {
  const out: Map<string, string> = new Map<string, string>();
  for (const raw of text.split("\n")) {
    let line = raw.trim();
    if (line.length === 0 || line.startsWith("#")) {
      continue;
    }
    if (line.startsWith("export ")) {
      line = line.slice(7, line.length).trim();
    }
    const eq = line.indexOf("=");
    if (eq <= 0) {
      continue;
    }
    const key = line.slice(0, eq).trim();
    let value = line.slice(eq + 1, line.length).trim();
    if ((value.startsWith("\"") && value.endsWith("\"") && value.length >= 2) || (value.startsWith("'") && value.endsWith("'") && value.length >= 2)) {
      value = value.slice(1, value.length - 1);
      value = value.replaceAll("\\n", "\n");
    } else {
      const hash = value.indexOf(" #");
      if (hash >= 0) {
        value = value.slice(0, hash).trim();
      }
    }
    out.set(key, value);
  }
  return out;
}

@Module({ providers: [ConfigService], exports: [ConfigService] })
export class ConfigModule {
  /** Loads the .env file (unless ignoreEnvFile) and provides ConfigService. */
  static forRoot(options: ConfigModuleOptions = {}): DynamicModule {
    if (!(options.ignoreEnvFile || false)) {
      const given = options.envFilePath;
      const paths: string[] = given == null ? [".env"] : typeof given === "string" ? [given] : (given as string[]);
      for (const path of paths) {
        if (existsSync(path)) {
          const vars = parseEnv(readFileSync(path, "utf8"));
          for (const [k, v] of vars) {
            if (process.env.get(k) === null) {
              process.env.set(k, v);
            }
          }
        }
      }
    }
    for (const f of options.load ?? []) {
      build(f);
    }
    return { module: ConfigModule, providers: [ConfigService], exports: [ConfigService], global: options.isGlobal || false };
  }

  /**
   * Provides one configuration namespace under its KEY, for @Inject(cfg.KEY), and loads it for get("ns.key").
   * @example imports: [ConfigModule.forFeature(redisConfig)]
   */
  static forFeature(config: unknown): DynamicModule {
    // built when first injected, as Nest's factory runs once the modules are imported (the .env is read by then)
    const p: FactoryProvider = {
      provide: `CONFIGURATION(${namespaceOf(config)})`,
      useFactory: (): unknown => build(config),
    };
    return { module: ConfigModule, providers: [p, ConfigService], exports: [p, ConfigService] };
  }
}
