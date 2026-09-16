# ForgeFP Practice

A set of exercises for learning [ForgeFP](../.docs/README.md) — like LeetCode,
but every problem is designed to be solved *with the library* rather than with
raw loops and hand-rolled boilerplate.

## How to use this

You solve a problem by writing the code that makes its `assert(...)` line pass.
There is no test runner and no hidden grading — an `assert` that fails aborts
the program; one that passes does nothing. **"Runs without printing anything"
== "solved".**

### Step by step

1. **Pick a problem.** Open one of the files under `problems/`. Each problem
   is a title, a difficulty, the header(s) you need, and a concrete example
   written as an `assert(...)`.

2. **Create a solution file.** Put it anywhere (e.g. `my-solutions/01.cpp`) and
   start from this template:

   ```cpp
   #include <fp/vec.hpp>     // the header(s) listed at the top of the topic file
   #include <cassert>
   #include <vector>

   int main() {
       // copy the assert from the problem, then write the code above it
       assert(/* expected result */);
   }
   ```

3. **Write the code** so the `assert` holds.

4. **Compile and run** from the repo root (`/root/C++/fp`):

   ```bash
   g++ -std=c++20 -I src -o /tmp/solution my-solutions/01.cpp && /tmp/solution
   ```

   Silence = correct. An `Assertion ... failed` message = wrong.

5. **Stuck?** Expand the `<details>` under **Solutions** at the bottom of the
   topic file and compare.

### A worked example (01-warmup, problem 1)

The problem says:

> Given `std::vector<int>{1,2,3,4}`, produce `{2,4,6,8}` using `fp::map`.
>
> ```cpp
> assert(fp::map(std::vector<int>{1,2,3,4}, [](int x){ return x*2; })
>        == std::vector<int>({2,4,6,8}));
> ```

Your solution file, `my-solutions/01-double.cpp`:

```cpp
#include <fp/vec.hpp>
#include <cassert>
#include <vector>

int main() {
    auto out = fp::map(std::vector<int>{1, 2, 3, 4},
                       [](int x) { return x * 2; });
    assert(out == std::vector<int>({2, 4, 6, 8}));
}
```

Build and run:

```bash
g++ -std=c++20 -I src -o /tmp/01-double my-solutions/01-double.cpp && /tmp/01-double
# no output = correct
```

### Compiler flags per topic

| Topic | Extra flags |
|---|---|
| 01–07 (warmup … parsing) | none |
| 08-concurrency | `-pthread` |
| 09-memory-simd | `-O2 -march=native` |

Base command everywhere: `g++ -std=c++20 -I src -o out file.cpp`.

### Tips

- The header list at the top of each topic file is the whole dependency; if
  you'd rather not think about it, `#include <fp/all.hpp>` covers topics 1–8
  (but *not* `simd.hpp` or `macros.hpp`).
- Add extra `assert`s of your own to be thorough — the given one is the minimum.
- If your solution is more than a few lines, you're probably fighting the
  library (see **Rules of the game** below).

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
