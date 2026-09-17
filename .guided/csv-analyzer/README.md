# Project 1 — CSV Analyzer

Build a small CLI that reads a CSV of numbers, cleans it, and reports
statistics. This project introduces the **core verbs** (`map`/`filter`/`fold`)
and the central idea of **error-as-value** (`Result`).

**Modules:** `vec.hpp`, `ranges.hpp`, `string.hpp`, `io.hpp`, `result.hpp`.
**Compile:** `g++ -std=c++20 -I src -o app app.cpp && ./app data.csv`

Create a test file `data.csv`:

```
1,2,3
4,5,6
7,bad,9
```

---

## Step 1 — Read the file and split lines

The whole pipeline starts with `read_lines`, which returns a `Result` — the file
may not exist, so "the lines" is a *value you might not have*.

```cpp
#include <fp/io.hpp>
#include <iostream>

int main() {
    auto lines = fp::read_lines("data.csv");
    if (!lines.is_ok()) {
        std::cerr << lines.error() << "\n";
        return 1;
    }
    std::cout << lines.value().size() << " lines\n";
}
```

**Concept — error-as-value:** instead of `try`/`catch`, the failure is the
return value. You *must* check `is_ok()`; that's the whole point.

## Step 2 — Turn lines into cells

Each line is comma-separated. `flat_map` maps each line to a *vector* of cells
and stitches the results into one flat vector.

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

**Concept — composition:** `flat_map` is `map` + flatten. "Map every line to a
list, then join the lists" is two ideas you don't have to express separately.

## Step 3 — Parse the cells, tolerating bad ones

`to_int` returns `Result<int>`. To parse a whole vector *and keep the successes*,
use `filter_map`: it applies a function returning `optional` and drops the
empties. First convert each `Result` to an `optional`:

```cpp
#include <fp/io.hpp>
#include <fp/vec.hpp>
#include <fp/ranges.hpp>
#include <fp/string.hpp>
#include <fp/result.hpp>
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

**Concept — mapping that can fail:** `filter_map` keeps the present results.
You wrote the "may fail" logic once (`try_parse`) and reused it for every cell.

## Step 4 — Compute statistics with folds

Now reduce the numbers. `sum`, `maximum`, `minimum`, and a manual `fold_left`
for the mean.

```cpp
#include <fp/io.hpp>
#include <fp/vec.hpp>
#include <fp/ranges.hpp>
#include <fp/string.hpp>
#include <fp/result.hpp>
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
    if (numbers.empty()) {
        std::cout << "no numbers\n";
        return 0;
    }

    int total = fp::sum(numbers);
    double mean = static_cast<double>(total) / numbers.size();
    int lo = *fp::minimum(numbers);
    int hi = *fp::maximum(numbers);

    std::cout << "count=" << numbers.size()
              << " sum=" << total
              << " mean=" << mean
              << " min=" << lo
              << " max=" << hi << "\n";
}
```

**Concept — reduce:** a statistic is a fold of the collection. `sum`/`min`/`max`
are named folds; `mean` is "sum then divide" — two steps composed.

## Step 5 — Pipe it into one expression

The whole program is one value flowing through stages. Rewrite it with `into`/`|`:

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
    if (!lines.is_ok()) { std::cerr << lines.error() << "\n"; return 1; }

    auto numbers = fp::out(
        fp::into(lines.value())
        | [](auto lines) {
              return fp::flat_map(lines, [](std::string const& l) { return fp::str::split(l, ','); });
          }
        | [](auto cells) { return fp::filter_map(cells, try_parse); });

    // ... stats as in step 4
}
```

**Concept — the pipe:** `into(x) | f | g | out` reads top-to-bottom as "lines →
cells → numbers". Each stage is a pure function; the value just flows.

---

## 🏆 Challenge

Extend the analyzer on your own. Pick one or more:

1. **Add `median`** — sort the numbers, take the middle (or the mean of the two
   middles). Use `fp::sort`.
2. **Add `--filter N`** — a command-line flag that only *keeps* numbers `> N`
   before computing stats (use `fp::filter` + `fp::gt`).
3. **Handle a header row** — if the first line is `"x,y,z"`, skip it (use
   `fp::tail`).
4. **Report how many cells were *bad*** — instead of silently dropping them,
   count the failures (keep a running tally as you `filter_map`).

**Hints:**
- `fp::sort` returns a sorted copy (input unchanged).
- `fp::tail(v)` is "all but the first".
- For the bad-count, `filter_map` drops them for you — so do the count as a
  separate pass, or replace `filter_map` with a fold that carries both the good
  values and a failure count.

There's no model solution — make it do something useful to *you*. The only rule:
prefer `fp::` combinators over raw loops.
