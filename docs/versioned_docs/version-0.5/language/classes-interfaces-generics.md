---
title: Classes, interfaces and generics
sidebar_position: 6
---

# Classes, interfaces and generics

Classes compile to fixed-layout structs. Methods are plain functions. Generics are compiled once per type list.

## Classes

Fields, constructors, getters, `static` members, parameter properties (`constructor(private r: number)`),
`abstract` classes and methods, and single inheritance work as in TypeScript:

```ts
abstract class Shape {
  abstract area(): number;
  describe(): string { return `area ${this.area().toFixed(2)}`; }
}
class Square extends Shape {
  protected side: number;
  constructor(s: number) { super(); this.side = s; }
  area(): number { return this.side * this.side; }
}
const shapes: Shape[] = [new Square(2)];
for (const s of shapes) console.log(s.describe());   // area 4.00
```

- **Dispatch only where needed.** A method that no subclass overrides is a direct call. An overridden method
  dispatches on the object's class.
- **`private` and `protected`** are checked at compile time and cost nothing at run time.
- **Every field without an initializer** must be assigned in the constructor. A class with `x!: T` fields is
  filled from JSON and cannot be built with `new` (XS8009).
- **`instanceof`** tests the run-time class; `base as Sub` is checked (`TypeError: the value is not Sub`).

## Interfaces

An interface of data (fields only) is a shape. A class object passed where such an interface goes is **copied**
field by field, so writes through the parameter do not reach the caller:

```ts
interface Point { x: int; y: int }
class P { x: int = 1; y: int = 2; extra = "z"; }
function move(q: Point): void { q.x = 99; }
const p = new P(); move(p);
console.log(p.x);   // xisto: 1, Node: 99
```

An interface with methods is matched in two ways:

| Class | Where the interface goes |
|---|---|
| `class Cat implements Named` | the object itself; `===` holds |
| a class that fits but does not say `implements` | an adapter object made per conversion; `===` with the original is false |

Write `implements` on the classes you pass as an interface: it is the fast path and keeps identity.

## Generics

Generic functions, classes and interfaces are monomorphised: `Stack<string>` and `Stack<int>` are two compiled
classes, with no boxing and no run-time cost.

```ts
class Stack<T> { private items: T[] = []; push(x: T): void { this.items.push(x); } pop(): T | null { return this.items.pop() ?? null; } }
```

A generic **arrow value** held in a variable inside a function is erased instead: its `T` is read as `unknown`.
A result that must become a concrete type is copied through JSON, with warning XS2110:

```text
warning[XS2110]: the result of the generic function value 'firstRow' is a copy of Row made through JSON
  help: … give the function a concrete return type, or make it a generic function or method instead of a value
```

A type parameter that cannot be inferred (`empty()` for `function empty<T>(): T[]`) becomes `unknown` with
warning XS2104; pass it explicitly: `empty<string>()`.

## Unions and discriminants

Unions of classes narrow with `instanceof`, unions of primitives with `typeof`, and discriminated unions on a
literal field:

```ts
type Result = { kind: "ok"; value: int } | { kind: "err"; message: string };
function show(r: Result): string { return r.kind === "ok" ? `ok ${r.value}` : `err ${r.message}`; }
```

## Tuples

`[string, int]` is a fixed tuple; destructuring and `console.log` behave as on Node. `Promise.all` over calls of
different types gives a tuple: `const [x, y] = await Promise.all([a(), b()])`.

## See also

- [any](./any.md)
- [Decorators](./decorators.md)
- [Divergences from TypeScript](./divergences.md)
- [Known limitations](./known-limitations.md)
