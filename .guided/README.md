# ForgeFP — Guided Projects

Seven focused projects plus a final **capstone** that pulls the whole library
into one program. Each step gives you most of the code and explains the
**functional concept** behind it; each project ends with an open **challenge**
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
| [8. input-repl](input-repl/README.md) | a line processor that becomes a REPL | streams, producer/consumer, key reading | `input`, `stream`, `concurrent`, `adt` |
| [9. spinner](spinner/README.md) — **capstone** | a rotating shape in the terminal | every concept above, combined | *all of them* |
| [10. json-parser](json-parser/README.md) | a JSON parser | parser combinators, recursion (`ref`) | `parse`, `adt`, `variant` |

## Suggested order

Do them 1 → 8, then the **spinner** (9) as the capstone — it assumes the whole
toolkit. Project 10 (**json-parser**) is the parsing capstone; do it any time
after 2–3. The eight lead-ups build on each other: 1 introduces the core verbs,
2–3 the ADTs and parsing, 4 the parallel combinators, 5 the higher-order
functions, 6–7 the advanced performance tools, 8 the input/stream layer. The
spinner uses all of them at once.

## The pattern

Every project follows the same rhythm:

1. **Model** — decide what a "thing" is (`Result`, a variant, a struct).
2. **Transform** — write pure functions that turn one value into another.
3. **Compose** — thread them together with `map`/`and_then`/`|`.
4. **Challenge** — extend it yourself.

### The `|` operator (use it everywhere)

The pipe is type-directed: `into(x) | f` maps `f` over the value when `x` is a
wrapper (`Result`/`Either`/`Validation`, `optional`, `Stream`), and applies `f`
directly otherwise. So the same operator chains an `int`, a `Result`, and a
`Stream`:

```cpp
fp::out(fp::into(3) | fp::times(2));                       // 6
fp::out(fp::into(fp::ok(3)) | fp::times(2));               // ok(6) — error propagates
fp::out(fp::into(std::optional<int>{3}) | fp::plus(1));    // optional{4}
fp::out(fp::into(stream) | fp::times(2));                  // a mapped Stream
```

Use it in place of `map`/`and_then` wherever a wrapper value flows through the
pipeline — see [composition](../.docs/composition.md) for the full rules.

If a step's code doesn't make sense, read the linked [docs](../.docs/README.md)
for that module — the walkthroughs assume you can look up the exact signatures.
