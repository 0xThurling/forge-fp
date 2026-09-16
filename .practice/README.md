# ForgeFP Practice

A set of exercises for learning [ForgeFP](../.docs/README.md) — like LeetCode,
but every problem is designed to be solved *with the library* rather than with
raw loops and hand-rolled boilerplate.

## How to use this

1. Pick a topic file under `problems/`.
2. Read a problem, write a `main()` that produces the expected output, and
   check it against the given assertions (each problem includes a concrete
   example with the expected result).
3. When stuck, expand the solution under **Solutions** at the bottom of the
   file (each is collapsed behind a `<details>` tag so you can avoid spoilers).

Compile any check with:

```bash
g++ -std=c++20 -I src -o check check.cpp && ./check
```

(add `-pthread` for concurrency problems, `-O2 -march=native` for SIMD).

## Difficulty

| Level | Meaning |
|---|---|
| Easy | One combinator, one call. Warm-up for a module. |
| Medium | Compose two or three combinators; thread a `Result`/`optional`. |
| Hard | A pipeline, a grammar, or a concurrency/memory concern. |

## Suggested path

1. **01-warmup** — `map`/`filter`/`fold`/`ops` (the vocabulary).
2. **02-collections** — `zip`/`group_by`/`scan`/`chunk`/grids.
3. **03-strings** — `split`/`join`/`to_int`.
4. **04-adts** — `Result`/`Either`/`Maybe`/`Validation` (the core idea).
5. **05-composition** — `compose`/`pipe`/`curry`/`memoize`.
6. **06-pattern-matching** — `match`/`case_`/`cond`.
7. **07-parsing** — parser combinators.
8. **08-concurrency** — `par_map`/`RingBuffer`/`Actor`/`race`.
9. **09-memory-simd** — `Arena` and vectorized mapping.

Each topic file lists the header(s) you'll need at the top.

## Rules of the game

- Prefer `fp::` combinators over raw `for` loops — that's the point.
- Errors flow through `Result`, not exceptions (unless the problem says to
  `unwrap`).
- If a solution is longer than a handful of lines, you're probably fighting
  the library instead of using it.
