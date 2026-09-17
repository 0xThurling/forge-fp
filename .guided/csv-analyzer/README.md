# Project 1 — CSV Analyzer (CodeCrafters-style)

Build a CSV statistics CLI, one stage at a time. Each stage adds a feature and
ends with a **Verify** check you must make pass before moving on.

**Modules:** `io.hpp`, `string.hpp`, `vec.hpp`, `ranges.hpp`, `result.hpp`.
**Compile:** `g++ -std=c++20 -I src -o app app.cpp && ./app data.csv`

Your `data.csv`:

```
1,2,3
4,5,6
7,not-a-number,9
```

---

## Stage 1 — Read the file

Goal: load the file's lines, or report the error.

```cpp
#include <fp/io.hpp>
#include <iostream>

int main() {
    auto lines = fp::read_lines("data.csv");
    if (!lines.is_ok()) {
        std::cerr << lines.error() << "\n";   // "cannot open data.csv" if missing
        return 1;
    }
    std::cout << lines.value().size() << " lines\n";
}
```

**Verify:** prints `3 lines` (and a clean error if you rename the file).

**Concept — error-as-value.** `read_lines` returns `Result<vector<string>>`: the
failure is the value, not an exception. You *must* handle `!is_ok()`, which is
the whole point.

## Stage 2 — Split each line into cells

Goal: turn lines into a flat list of cells.

```cpp
#include <fp/io.hpp>
#include <fp/vec.hpp>
#include <fp/string.hpp>
#include <iostream>

int main() {
    auto lines = fp::read_lines("data.csv");
    if (!lines.is_ok()) return 1;

    auto cells = fp::flat_map(lines.value(), [](std::string const& line) {
        return fp::str::split(line, ',');
    });
    std::cout << cells.size() << " cells\n";
}
```

**Verify:** prints `9 cells`.

**Concept — `flat_map` = map + flatten.** Each line maps to a *vector* of cells;
`flat_map` stitches the vectors into one. Two ideas in one call.

## Stage 3 — Parse the cells, tolerating garbage

Goal: keep only the cells that are actually numbers.

```cpp
#include <fp/all.hpp>
#include <optional>
#include <iostream>

std::optional<int> try_parse(std::string const& s) {
    auto r = fp::str::to_int(s);
    return r.is_ok() ? std::optional<int>{r.value()} : std::nullopt;
}

int main() {
    auto lines = fp::read_lines("data.csv");
    if (!lines.is_ok()) return 1;
    auto cells = fp::flat_map(lines.value(), [](std::string const& line) {
        return fp::str::split(line, ',');
    });

    auto numbers = fp::filter_map(cells, try_parse);
    for (int n : numbers) std::cout << n << " ";
    std::cout << "\n";
}
```

**Verify:** prints `1 2 3 4 5 6 7 9` (the `not-a-number` cell is gone).

**Concept — mapping that can fail.** `try_parse` turns the failure into
`nullopt`; `filter_map` keeps only the present results.

## Stage 4 — Count and sum

Goal: report how many valid numbers and their sum.

```cpp
// ... same loading as stage 3 ...
int total = fp::sum(numbers);
std::cout << "count=" << numbers.size() << " sum=" << total << "\n";
```

**Verify:** `count=8 sum=37`.

**Concept — reduce.** `sum` is a named fold. The count is just `size()`.

## Stage 5 — Min and max

Goal: report the range.

```cpp
int lo = *fp::minimum(numbers);   // optional<int> — deref (we know it's non-empty)
int hi = *fp::maximum(numbers);
std::cout << "min=" << lo << " max=" << hi << "\n";
```

**Verify:** `min=1 max=9`.

**Concept — `optional` results.** `minimum`/`maximum` return `optional` because
an empty vector has no min/max. Deref only after you've checked non-empty.

## Stage 6 — Mean

Goal: compute the average.

```cpp
double mean = static_cast<double>(total) / numbers.size();
std::cout << "mean=" << mean << "\n";
```

**Verify:** `mean=4.625`.

**Concept — compose.** The mean is "sum then divide" — two pure steps, no new
combinator needed.

## Stage 7 — Median

Goal: the middle value (or mean of the two middles).

```cpp
double median(std::vector<int> xs) {
    if (xs.empty()) return 0.0;
    xs = fp::sort(xs);                       // sorted copy, input unchanged
    size_t n = xs.size();
    return n % 2 ? xs[n / 2] : (xs[n / 2 - 1] + xs[n / 2]) / 2.0;
}
// ...
std::cout << "median=" << median(numbers) << "\n";
```

**Verify:** `median=4.5`.

**Concept — purity again.** `fp::sort` returns a new vector; you can sort a copy
inside `median` without clobbering the caller's data.

## Stage 8 — A `--min N` filter flag

Goal: read a threshold from `argv` and keep only numbers above it.

```cpp
int main(int argc, char** argv) {
    int threshold = 0;
    if (argc == 3 && std::string(argv[1]) == "--min")
        threshold = std::stoi(argv[2]);

    // ... load numbers ...
    auto kept = fp::filter(numbers, fp::gt(threshold));
    // ... report stats on `kept` ...
}
```

**Verify:** `./app data.csv --min 4` keeps `{4,5,6,7,9}`.

**Concept — `fp::gt(N)` as a predicate.** The named operator `gt(threshold)` *is*
"greater than threshold", so `filter(numbers, gt(threshold))` reads like prose.

## Stage 9 — Skip a header row

Goal: if the first line is a header (e.g. starts with `#` or is non-numeric),
skip it.

```cpp
auto body = fp::tail(lines.value());   // all but the first line
```

**Verify:** a file with a header line now produces correct numbers.

**Concept — `tail` = all-but-first.** Like `head`/`last`/`init`, it's the list
vocabulary that replaces index arithmetic.

## Stage 10 — Report the bad cells

Goal: don't silently drop garbage — count it.

```cpp
int bad = 0;
std::vector<int> numbers;
for (auto const& cell : cells) {
    if (auto r = fp::str::to_int(cell); r.is_ok()) numbers.push_back(r.value());
    else ++bad;
}
std::cout << "valid=" << numbers.size() << " bad=" << bad << "\n";
```

**Verify:** `valid=8 bad=1`.

**Concept — explicit effects.** This is the *imperative* version of stage 3's
`filter_map`; you're choosing to surface the failure count instead of hiding it.
Both are legitimate — pick per problem.

---

## 🏆 Extensions

Take it as far as you like. Each is self-contained:

1. **Multiple columns** — parse a row of `x,y` pairs and report per-column
   stats (`zip`/`unzip` the columns first).
2. **`--sort` output** — print the numbers sorted, descending, deduplicated
   (`fp::sort`, `fp::reverse`, `fp::unique`).
3. **Table output** — align columns with `fp::str::pad_right`/`pad_left`.
4. **Stream large files** — process the lines lazily with a `Stream`
   (`stream.hpp`) so you don't hold the whole file in memory.
5. **A histogram** — bucket the numbers into ranges with `fp::group_by` and
   print a bar per bucket.

The only rule: prefer `fp::` combinators over raw loops.
