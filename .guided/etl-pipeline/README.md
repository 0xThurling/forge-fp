# Project 4 — ETL Pipeline (CodeCrafters-style)

Build a miniature ETL — extract → transform → load — one stage at a time, then
parallelize the heavy parts. Each stage adds a stage of the pipeline and ends
with a **Verify** check.

**Modules:** `io.hpp`, `string.hpp`, `vec.hpp`, `ranges.hpp`, `map.hpp`, `concurrent.hpp`, `compose.hpp`.
**Compile:** `g++ -std=c++20 -pthread -I src -o app app.cpp && ./app`

`people.csv`:

```
alice,32
bob,17
carol,28
dave,41
alice,35
```

---

## Stage 1 — Model a record

Goal: a `Person` is plain data; parsing is a pure `string -> Result<Person>`.

```cpp
#include <fp/all.hpp>

struct Person { std::string name; int age; };

fp::Result<Person> parse_person(std::string const& line) {
    auto parts = fp::str::split(line, ',');
    if (parts.size() != 2) return fp::err<Person>("expected 'name,age'");
    auto age = fp::str::to_int(parts[1]);
    if (!age.is_ok()) return fp::err<Person>("bad age");
    return fp::ok(Person{parts[0], age.value()});
}
```

**Verify:** `parse_person("alice,32")` is `ok({"alice",32})`; `parse_person("x")` is an error.

**Concept — model as data.** `Person` is dumb; `parse_person` is a pure function
whose failure is a `Result`.

## Stage 2 — Extract: parse the whole file, drop bad rows

Goal: load lines and parse each, keeping only the valid ones.

```cpp
#include <optional>

std::optional<Person> try_person(std::string const& line) {
    auto r = parse_person(line);
    return r.is_ok() ? std::optional<Person>{r.value()} : std::nullopt;
}

int main() {
    auto lines = fp::read_lines("people.csv");
    if (!lines.is_ok()) return 1;
    auto people = fp::filter_map(lines.value(), try_person);
    for (auto const& p : people) std::cout << p.name << " " << p.age << "\n";
}
```

**Verify:** prints the 5 valid rows.

**Concept — extract = parse + filter.** `filter_map` applies the fallible parse
and keeps the successes.

## Stage 3 — Transform: keep only adults

Goal: a pure filter stage.

```cpp
auto adults = fp::filter(people, [](Person const& p) { return p.age >= 18; });
```

**Verify:** `adults.size() == 4` (bob, 17, is dropped).

**Concept — a stage is a function.** Each stage takes a collection and returns a
new one — no shared state between stages.

## Stage 4 — Transform: enrich with a computed field

Goal: add a field (here, "age in a decade") via `map`.

```cpp
struct Enriched { Person person; int decade; };

auto enriched = fp::map(adults, [](Person const& p) {
    return Enriched{p, p.age / 10};
});
```

**Verify:** `enriched[0].decade == 3` for alice (32).

**Concept — `map` changes shape.** One input type in, another out; the pipeline
stays a sequence of pure transforms.

## Stage 5 — Aggregate: group by decade

Goal: bucket people by their decade.

```cpp
auto by_decade = fp::group_by(enriched, [](Enriched const& e) { return e.decade; });
for (auto const& [decade, group] : by_decade)
    std::cout << decade << "0s: " << group.size() << "\n";
```

**Verify:** `3 → 2` (alice, carol), `4 → 1` (dave).

**Concept — aggregation is a fold into a map.** `group_by(v, key_fn)` is that
fold, named.

## Stage 6 — Aggregate: fold a summary

Goal: reduce each group to a number (average age).

```cpp
for (auto const& [decade, group] : by_decade) {
    int total = fp::fold_left(group, 0, [](int acc, Enriched const& e) { return acc + e.person.age; });
    double avg = static_cast<double>(total) / group.size();
    std::cout << decade << "0s: " << group.size() << " people, avg age " << avg << "\n";
}
```

**Verify:** the 30s group has avg age `30`.

**Concept — `fold_left`.** The general reducer; you supply the accumulator logic.

## Stage 7 — Parallelize the heavy transform

Goal: a deliberately heavy per-record transform, run across a `ThreadPool`.

```cpp
int score(Person const& p) {
    int acc = 0;
    for (int i = 0; i < 100000; ++i) acc += p.age ^ i;
    return acc;
}

int main() {
    // ... load people ...
    fp::ThreadPool pool(4);
    auto scores = fp::par_map(pool, people, score);   // same order as `people`
}
```

**Verify:** `scores.size() == people.size()` and order matches.

**Concept — parallelism is a drop-in.** `par_map` has the same contract as `map`
(same order, same result), just faster. Purity is what makes it safe — `score`
writes nothing shared.

## Stage 8 — Aggregate in parallel: `par_reduce`

Goal: sum the scores in parallel.

```cpp
long long total = fp::par_reduce(pool, scores, 0, fp::plus);
```

**Verify:** `total` equals a sequential `fold_left` of the scores.

**Concept — associativity.** `par_reduce` reorders the reduction, so `op` must be
associative (`+` is). For non-associative ops, stay sequential.

## Stage 9 — Join: enrich from a second file

Goal: merge `people.csv` with `cities.csv` (`name,city`) using `to_map` + `lookup`.

```cpp
// cities.csv -> std::map<std::string, std::string> (name -> city)
auto city_map = fp::to_map<std::string, std::string>(pairs);

auto with_city = fp::map(people, [&](Person const& p) {
    auto city = fp::lookup(city_map, p.name);
    return city ? p.name + " (" + *city + ")" : p.name;
});
```

**Verify:** each person is enriched with their city (or unchanged if absent).

**Concept — join via a lookup map.** A `map` is the join index; `lookup` is the
keyed access; `map` applies it. The whole join is three combinators.

## Stage 10 — The pipeline as a value

Goal: name the whole pipeline with `fp::pipe` and apply it.

```cpp
auto pipeline = fp::pipe(
    [](auto lines) { return fp::filter_map(lines, try_person); },
    [](auto people) { return fp::filter(people, [](Person const& p) { return p.age >= 18; }); },
    [](auto people) { return fp::map(people, score); });

auto scores = pipeline(lines.value());
```

**Verify:** `scores` matches the staged version.

**Concept — composition.** `pipe` turns the staged functions into one reusable
function. The pipeline is now a *value* you can pass around, test, and reuse.

---

## 🏆 Extensions

1. **Streaming** — rewrite the extract as a `Stream` so rows are processed
   lazily (`stream.hpp`), not all materialized at once.
2. **Error reporting** — don't drop bad rows silently; collect their messages
   (`collect_all` or a `Validation`) and print a summary.
3. **A `--key name` flag** — group by an arbitrary field chosen at runtime.
4. **Deduplicate** — drop duplicate names (`fp::unique` after `sort_by`).
5. **Multiple passes** — run the pipeline over several files and combine the
   results (`concat`).

The goal is an ETL you could point at your own data, with the heavy stages
parallel and the stages themselves pure, named functions.
