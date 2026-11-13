---
title: any
sidebar_position: 5
---

# any

`any` compiles. It takes a dynamic path: each access is looked up by name and checked at run time. The typed path
stays the default, and every place that uses `any` gets warning XS2007, so the cost is never hidden.

```text
x.ts:2:10: warning[XS2007]: 'any' takes the dynamic path: each access is looked up by name and checked at run time (about 10x slower than a typed access)
     2 | const u: any = new User();
  help: declare a type (an interface, a class, Record<string, T>) to take the typed path
```

## What happens to the value

| Value given to `any` | Stored as | Field access |
|---|---|---|
| an object of a class | the same object, same layout, no extra bytes | through a per-class table of field names |
| an object made as `any` (`const o: any = { … }`) | a `Record<string, unknown>` | a hash lookup; more memory than a typed object |
| a primitive | a tagged value | |

The Record case has its own warning text: "…and it is stored as a Record, which uses more memory than a typed
object".

## What works

```ts
class User { name = "ana"; age: int = 30; greet(): string { return "hi " + this.name; } }
const u: any = new User();
console.log(u.name, u.age + 1);   // ana 31
console.log(u.greet());           // hi ana (warning: method looked up by name)
const o: any = { id: 1, tags: ["a"] };
o.extra = true;
console.log("extra" in o, JSON.stringify(o));   // true {"id":1,"tags":["a"],"extra":true}
const back: User = u;             // checked: u must be a User
```

Field reads and writes, `x[k]`, `k in x`, arithmetic, `===`, truthiness, `??`, `?.`, templates, JSON and
`console.log` all work. A method call on an `any` is looked up among the classes that declare that method.

## Run-time checks

An `any` meets a typed slot (a variable, a parameter, a field) through a check. A mismatch is a `TypeError`:

```ts
const bad: string = o.id;   // TypeError: the value is not string (Node goes on with 1)
```

A field that the object does not have reads as `null` (printed as `null`; Node prints `undefined`).

## The cost

Reading a field through `any` costs ten times a typed read or more. Measured on an Apple M4 (12 Nov 2026), a loop
of 10 million reads: 0.4 ns per typed read, 9 ns per read through `any`. A typed read compiles to one load; an
`any` read goes through a name lookup and a type check.

## Keeping `any` out

- `xisto build` prints the count of warnings (for example `xisto: 4 warnings (XS2007 4)`); `--warnings` lists
  them.
- `xisto check` reports them in a `dynamic-path` category; `--details` shows each place.
- `--deny-warnings` turns them into errors, for CI.

## Migrating off `any`

1. Run `xisto check --details` and start with the hottest code.
2. Replace `any` with the class or interface the value really has. For bags of fields, use
   `Record<string, T>`.
3. For a method called on an `any`, cast once: `(x as Repo).find(id)`.
4. For decoded JSON, declare the type: `const t: Todo = JSON.parse(text)` decodes straight into the type.

## See also

- [Warnings](./warnings.md)
- [Classes, interfaces and generics](./classes-interfaces-generics.md)
- [Best practices](/docs/performance/best-practices)
