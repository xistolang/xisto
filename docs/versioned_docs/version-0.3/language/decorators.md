---
title: Decorators
sidebar_position: 11
---

# Decorators

There is no `reflect-metadata` at run time. Instead, the compiler builds a static description of each decorated
class, a `ClassInfo`, and passes it to the decorators. Undecorated classes carry nothing.

## Static reflection

`ClassInfo` and its parts come from `xisto:reflect`:

| Interface | Fields |
|---|---|
| `ClassInfo` | `id`, `name`, `params`, `construct(args)`, `methods`, `fields`, `isInstance(v)`, `base` |
| `MethodInfo` | `name`, `params`, `returns`, `invoke(self, args)` |
| `FieldInfo` | `name`, `optional`, `type`, `get(target)` |
| `TypeInfo` | `kind`, `id`, `decode(json)`, `encode(value)`, `schema` (JSON Schema, OpenAPI 3.0 dialect) |

A class decorator in xisto's form takes the `ClassInfo`:

```ts
import { ClassInfo } from "xisto:reflect";
const registry: ClassInfo[] = [];
function Entity(): (c: ClassInfo) => void {
  return (c: ClassInfo) => { registry.push(c); };
}
@Entity()
class User { id: int = 0; name: string = ""; email: string | null = null; }

for (const c of registry) {
  console.log(c.name, c.fields.map((f) => `${f.name}: ${f.type.kind}`).join(", "));
  const u = c.construct([]) as User;          // User id: int, name: string, email: string
  console.log(c.isInstance(u));               // true
}
```

A method decorator in this form takes `(c: ClassInfo, m: MethodInfo)`. This is how NestJS-style frameworks
(routes, dependency injection, validation, Swagger schemas) are built on xisto: the information
`emitDecoratorMetadata` would give at run time is generated at compile time.

## Method decorators that replace the method

TypeScript's form `(target, key, descriptor)` works, including decorators that replace `descriptor.value`:

```ts
function Timed(): MethodDecorator {
  return (target: object, key: string | symbol, descriptor: PropertyDescriptor) => {
    const original = descriptor.value as (...args: unknown[]) => unknown;
    descriptor.value = function (this: unknown, ...args: unknown[]): unknown {
      console.log(`-> ${String(key)}(${args.join(", ")})`);
      return original.apply(this, args);
    };
    return descriptor;
  };
}
class Calc {
  base = 10;
  @Timed() add(x: number): number { return this.base + x; }
  twice(x: number): number { return this.add(x) * 2; }   // goes through the wrapper
}
```

Only a decorated method gets a slot: calls to it, from outside and through `this`, go through the slot to the
wrapper. Stacked decorators wrap bottom up. Every other method keeps its direct call, so code without such
decorators compiles to the same machine code.

## Prototype patching

Libraries that instrument a driver patch a prototype. xisto supports that form: inside a function,
`Object.getPrototypeOf(x)` where `x` comes from a method call on a parameter, then `proto.m = function …`:

```ts
function instrument(db: Db): void {
  const sample = db.repo();
  const proto = Object.getPrototypeOf(sample) as { find: (...args: unknown[]) => Promise<unknown> };
  const original = proto.find;
  proto.find = async function (this: unknown, ...args: unknown[]): Promise<unknown> {
    console.log("find", args[0]);
    return original.apply(this, args);
  };
}
```

After the patch, every `Repo.find` call goes through the new function, as on Node. A patched method is called
indirectly; avoid the pattern in hot code. Other prototype writes are refused (XS2017).

## See also

- [Classes, interfaces and generics](./classes-interfaces-generics.md)
- [Known limitations](./known-limitations.md)
