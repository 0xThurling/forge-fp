# Project 4 — ETL Pipeline

Build a miniature ETL (extract → transform → load) that reads records, cleans
and transforms them, aggregates the result, then parallelizes the heavy step.
This project introduces **pure transformations**, **map-reduce**, and the
**parallel combinators**.

**Modules:** `concurrent.hpp`, `vec.hpp`, `ranges.hpp`, `compose.hpp`, `string.hpp`, `result.hpp`.
**Compile:** `g++ -std=c++20 -pthread -I src -o app app.cpp && ./app`

Create `people.csv`:

```
alice,32
bob,17
carol,28
dave,41
```

---

## Step 1 — Model a record and load it

A record is a plain struct; loading returns `Result` because the file may not
exist. Each line is parsed into a record (name + age).

```cpp
#include <fp/all.hpp>
#include <iostream>

struct Person {
    std::string name;
    int age;
};

fp::Result<Person> parse_person(std::string const& line) {
    auto parts = fp::str::split(line, ',');
    if (parts.size() != 2)
        return fp::err<Person>("expected 'name,age'");
    auto age = fp::str::to_int(parts[1]);
    if (!age.is_ok())
        return fp::err<Person>("bad age");
    return fp::ok(Person{parts[0], age.value()});
}

int main() {
    auto lines = fp::read_lines("people.csv");
    if (!lines.is_ok()) { std::cerr << lines.error() << "\n"; return 1; }
    for (auto const& l : lines.value()) std::cout << l << "\n";
}
```

**Concept — model as data:** `Person` is dumb data; `parse_person` is a pure
`string -> Result<Person>`. The failure (bad line) is a `Result`, not an
exception.

## Step 2 — Extract and clean

"Extract" = parse every line, keeping only the valid ones. `filter_map` does
the parse-and-drop in one pass.

```cpp
#include <fp/all.hpp>
#include <optional>
#include <iostream>

struct Person { std::string name; int age; };

std::optional<Person> try_person(std::string const& line) {
    auto parts = fp::str::split(line, ',');
    if (parts.size() != 2) return std::nullopt;
    auto age = fp::str::to_int(parts[1]);
    return age.is_ok() ? std::optional<Person>{Person{parts[0], age.value()}}
                       : std::nullopt;
}

int main() {
    auto lines = fp::read_lines("people.csv");
    if (!lines.is_ok()) return 1;

    auto people = fp::filter_map(lines.value(), try_person);
    for (auto const& p : people) std::cout << p.name << " is " << p.age << "\n";
}
```

**Concept — extract = parse + filter:** `filter_map` applies a fallible parse and
keeps the successes. Rows that don't parse simply vanish.

## Step 3 — Transform and aggregate

Transform: keep only adults. Aggregate: compute the average age with a fold.

```cpp
#include <fp/all.hpp>
#include <optional>
#include <iostream>

struct Person { std::string name; int age; };

std::optional<Person> try_person(std::string const& line) {
    auto parts = fp::str::split(line, ',');
    if (parts.size() != 2) return std::nullopt;
    auto age = fp::str::to_int(parts[1]);
    return age.is_ok() ? std::optional<Person>{Person{parts[0], age.value()}}
                       : std::nullopt;
}

int main() {
    auto lines = fp::read_lines("people.csv");
    if (!lines.is_ok()) return 1;
    auto people = fp::filter_map(lines.value(), try_person);

    // transform: adults only
    auto adults = fp::filter(people, [](Person const& p) { return p.age >= 18; });

    // aggregate: average age (a fold)
    auto total_age = fp::fold_left(adults, 0, [](int acc, Person const& p) { return acc + p.age; });
    double avg = adults.empty() ? 0 : static_cast<double>(total_age) / adults.size();

    std::cout << adults.size() << " adults, avg age " << avg << "\n";
}
```

**Concept — a pipeline of pure stages:** `filter_map` → `filter` → `fold`. Each
stage is a pure function; the data flows one way. This is the whole ETL idea in
miniature.

## Step 4 — Group by a key

`group_by` buckets people by a computed key — here, age decade (`30s`, `40s`, …).

```cpp
#include <fp/all.hpp>
#include <optional>
#include <iostream>

struct Person { std::string name; int age; };

std::optional<Person> try_person(std::string const& line) {
    auto parts = fp::str::split(line, ',');
    if (parts.size() != 2) return std::nullopt;
    auto age = fp::str::to_int(parts[1]);
    return age.is_ok() ? std::optional<Person>{Person{parts[0], age.value()}}
                       : std::nullopt;
}

int main() {
    auto lines = fp::read_lines("people.csv");
    if (!lines.is_ok()) return 1;
    auto people = fp::filter_map(lines.value(), try_person);

    auto by_decade = fp::group_by(people, [](Person const& p) { return p.age / 10; });
    for (auto const& [decade, group] : by_decade)
        std::cout << decade << "0s: " << group.size() << " people\n";
}
```

**Concept — aggregation is a fold into a map:** `group_by(v, key_fn)` is that
fold, named. You supply the key function; the bucketing loop is handled.

## Step 5 — Parallelize the expensive step

Now make the transform parallel. `ThreadPool` + `par_map` chunks the work across
workers *and preserves order*. (A real ETL would parallelize the parse — here
we parallelize a deliberately heavier transform to see the point.)

```cpp
#include <fp/all.hpp>
#include <optional>
#include <iostream>

struct Person { std::string name; int age; };

std::optional<Person> try_person(std::string const& line) {
    auto parts = fp::str::split(line, ',');
    if (parts.size() != 2) return std::nullopt;
    auto age = fp::str::to_int(parts[1]);
    return age.is_ok() ? std::optional<Person>{Person{parts[0], age.value()}}
                       : std::nullopt;
}

// a deliberately "expensive" transform to make parallelism pay off
int score(Person const& p) {
    int acc = 0;
    for (int i = 0; i < 100000; ++i) acc += p.age ^ i;
    return acc;
}

int main() {
    auto lines = fp::read_lines("people.csv");
    if (!lines.is_ok()) return 1;
    auto people = fp::filter_map(lines.value(), try_person);

    fp::ThreadPool pool(4);
    auto scores = fp::par_map(pool, people, score);   // same order as `people`
    for (size_t i = 0; i < people.size(); ++i)
        std::cout << people[i].name << " -> " << scores[i] << "\n";
}
```

**Concept — parallelism is a drop-in:** `par_map` has the *same contract* as
`map` (same order, same result), just faster. Purity is what makes this safe:
`score` writes nothing shared, so the workers never contend.

---

## 🏆 Challenge

Build a real pipeline end to end. Pick a few:

1. **`par_reduce` the total** — compute the sum of all scores with
   `par_reduce` instead of `fold_left` (remember: the op must be associative).
2. **A `--min-age N` filter flag** — read `N` from `argv` and filter to
   `age >= N` before scoring (use `fp::filter` + `fp::ge`).
3. **Join two files** — load `people.csv` and `cities.csv` (name → city) and
   *enrich* each person with their city (hint: `to_map` + `lookup`, then `map`).
4. **Streaming instead of materializing** — rewrite the load as a `Stream`
   (see `stream.hpp`) so rows are processed lazily, not all held in memory at
   once.

**Hints:**
- `par_reduce(pool, v, init, op)` — each worker reduces its slice, then the
  partials combine; use an associative `op`.
- `fp::to_map<vector>(pairs)` (from `map.hpp`) builds a lookup map;
  `fp::lookup(m, k)` returns `optional<V>`.
- A `Stream` wraps a `std::function<optional<T>()>` and supports `map`/`filter`/
  `subscribe`.

The goal is an ETL you could point at your own data — load, clean, enrich,
aggregate — with the heavy parts parallel.
