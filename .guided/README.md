# ForgeFP — Guided Projects

Seven full projects that walk you through building a *real program* with the
library, one step at a time. Each step gives you most of the code and explains
the **functional concept** behind it; each project ends with an open **challenge**
where you write the code yourself.

Unlike the [practice problems](../.practice/README.md) (single assertions),
these are *whole programs* you build up, run, and extend — the way you'd learn
the library on a real codebase.

## How to use

1. Pick a project. Each lives in its own folder with a `README.md` walkthrough.
2. Read a step, paste/type the code into a file, compile and run it **before**
   moving on — every step is a working program.
3. Do the **challenge** at the end on your own; there are hints, but no full
   solution (that's the point).

Compile from the repo root (`/root/C++/fp`):

```bash
g++ -std=c++20 -I src -o app app.cpp && ./app
```

(add `-pthread` for the concurrency projects, `-O2 -march=native` for SIMD.)

## The projects

| Project | What you build | FP concepts | Modules |
|---|---|---|---|
| [1. csv-analyzer](csv-analyzer/README.md) | a CSV stats tool | purity, error-as-value, map/filter/fold, pipelines | `vec`, `ranges`, `str`, `io`, `result` |
| [2. config-parser](config-parser/README.md) | a config file parser + validator | parser combinators, accumulating errors | `parse`, `validation`, `result` |
| [3. shape-calculator](shape-calculator/README.md) | a shape area/perimeter tool | sum types, exhaustive dispatch | `adt`, `variant`, `parse` |
| [4. etl-pipeline](etl-pipeline/README.md) | a parallel data pipeline | pure transforms, map-reduce, parallelism | `concurrent`, `vec`, `compose` |
| [5. memo-lab](memo-lab/README.md) | dynamic programming tools | recursion, memoization, higher-order functions | `memoize`, `combinators`, `curry` |
| [6. event-loop](event-loop/README.md) | an actor/stream event system | state-as-value, message passing | `concurrent`, `stream`, `adt` |
| [7. render-loop](render-loop/README.md) | a frame loop with SIMD + arena | scoped memory, vectorized math | `arena`, `simd`, `concurrent` |

## Suggested order

Do them 1 → 7. They build on each other: 1 introduces the core verbs, 2–3 the
ADTs and parsing, 4 the parallel combinators, 5 the higher-order functions,
6–7 the advanced performance tools. Together they touch every module in the
library.

## The pattern

Every project follows the same rhythm:

1. **Model** — decide what a "thing" is (`Result`, a variant, a struct).
2. **Transform** — write pure functions that turn one value into another.
3. **Compose** — thread them together with `map`/`and_then`/`|`.
4. **Challenge** — extend it yourself.

If a step's code doesn't make sense, read the linked [docs](../.docs/README.md)
for that module — the walkthroughs assume you can look up the exact signatures.
