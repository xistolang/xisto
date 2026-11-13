---
title: Why Xisto
sidebar_position: 2
description: The manifesto behind Xisto, what we measure, and how we describe the project.
---

# Why Xisto

> Software that respects the hardware it runs on.

## The eight lines on the wall

1. **Measure before you claim.** Without a number, it is only talk.
2. **Running is what costs the most.** An airplane is hard to build, and then it flies for decades.
3. **Compile once, run millions of times.** The heavy work is ours, not the user's machine's.
4. **A budget is a test.** If the executable grows or memory goes up, the build fails.
5. **Old hardware is an official target.** If it runs well on a ten-year-old machine, nobody has to replace theirs.
6. **Every dependency is weight.** Only what pays its own fare gets on board.
7. **Today's code compiles twenty years from now.** Rewriting because you are forced to is waste too.
8. **A good error saves attempts.** For people and for AI.

## Why it exists

For at least two decades software has grown slower and heavier, and handed the bill to the hardware. The hardware
industry runs to make up for it, and that race is paid in energy, in ore pulled out of the ground and in
electronic waste. Much of it could be saved if software were written for the machine it runs on.

The biggest environmental gain is not the watt saved in the CPU. It is the computer that stays useful for more
years, because building a machine costs more resources than years of using it.

We have done this before. Lume, our native database IDE, does the work of a full SQL editor in a fraction of the
size and memory of the usual tools. Xisto applies the same idea one level down, to the language: TypeScript
syntax compiled to native code that is small, fast and safe.

## The airplane cost model

```text
total cost = cost to compile + cost of one run × number of runs
```

A program is compiled once and runs millions of times, on millions of machines. One more second of compilation
fades to nothing. One more millisecond per run multiplies until it is a power plant.

That is why Xisto compiles before the program runs, not while it runs. A JIT:

- redoes its optimization on every machine, every time the program starts;
- carries its own compiler in the memory of every process;
- needs to warm up, and a command-line tool never gets warm.

## Two modes

- **Development** (`xisto build --dev`): compiles in milliseconds with little optimization. It serves the loop of
  trying and fixing, for people and for AI agents.
- **Release** (the default): optimizes hard. It may take what it needs, because it happens once.

## What we measure

| Indicator | Why |
|---|---|
| Run time | It is where the resources are spent |
| Peak memory | It decides whether a program runs on a modest machine |
| Executable size | Download, disk and startup time |
| Energy per task | The number that really matters |
| Tokens spent by AI agents | Writing software also consumes resources |

The measuring bench came before the compiler. The same reference programs, written in TypeScript, run on Node,
on Bun and on Xisto, and the results are versioned in the repository, so every change shows whether the airplane
got lighter or heavier. See [Benchmarks](/docs/benchmarks).

### Same output, bit for bit

We only compare against programs that give the answer Node would give. The case that fixed the rule was n-body.
It needs `1/√x`, and the fastest Rust entries in the Benchmarks Game compute it with `rsqrtps`: an x86 instruction
that returns an *approximation* of the reciprocal square root in single precision (about 12 correct bits, in one
cycle instead of the roughly 20 of a square root and a division), refined by one Newton step to about 23 bits.
That is enough to print nine equal decimals, but the double it produces is not what `1 / Math.sqrt(x)` gives: the
last bits differ.

For us that is not a fair yardstick. Xisto computes `Math.sqrt` exactly and divides, because Node does, and the
compiler promises the same output on any machine, bit for bit. So we compare with the Rust versions that use an
exact square root, and leave out the two that use `rsqrtps`. The same promise is why Xisto never reorders a
floating-point sum, even where a hand-vectorized program would be faster.

## AI and tokens

AI writes TypeScript well, because TypeScript is everywhere in its training data. A new syntax would make it err
more and spend more tokens. What saves tokens is failing fewer times:

- precise compiler errors, readable by machines, with the fix suggested;
- local reasoning, with no implicit conversion and no hidden behaviour;
- one way to do each thing, with deterministic formatting;
- fast compilation in development mode;
- types that catch the mistake before the program runs.

## Cautions we keep for ourselves

- **Sustainability without a number is greenwashing.** We publish the results, the bad ones included.
- **One language does not save the planet.** The impact comes from the example, and from whoever copies the idea.
- **Efficiency can increase use.** When something gets cheap, people use more of it. That is why the life of the
  hardware weighs more than raw speed.

## How we describe ourselves

**Xisto is a strict subset of TypeScript, compiled to native code.** The syntax is TypeScript's, so the same
source runs on Node and Bun unchanged, which is how the test suite checks every output. What defines Xisto is what
it refuses and what it does with the types: instead of erasing them before running, it generates native code from
them. An `int` is a 64-bit register, a `number[]` a contiguous array, a class a struct of known size.

Three rules for talking about the project:

1. **Always say "a strict subset of TypeScript"**, never "TypeScript support" without the adjective. Whoever
   arrives expecting all of TypeScript trips on the first rule and leaves; whoever arrives knowing the rules sees
   the code run on Node too, and stays.
2. **The divergence table goes on the first page of any material**, not hidden away. It is what makes the
   sentence above honest: see [Divergences from TypeScript](/docs/language/divergences).
3. **"TypeScript" is a trademark of Microsoft.** We use it as a description ("a subset of TypeScript", "written
   in TypeScript"), never as the name of the product. The product is Xisto. The domain, `xisto-lang.org`, follows
   the convention of `golang.org` and `rust-lang.org`: "lang" names the language tooling project, not the promise of
   a new syntax.

*The Xisto Authors*
