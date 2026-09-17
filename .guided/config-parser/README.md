# Project 2 — Config Parser & Validator (CodeCrafters-style)

Build a parser for `key=value` config files, stage by stage, then validate the
result. Each stage adds grammar and ends with a **Verify** check.

**Modules:** `parse.hpp`, `validation.hpp`, `result.hpp`, `string.hpp`, `io.hpp`, `map.hpp`.
**Compile:** `g++ -std=c++20 -I src -o app app.cpp && ./app config.txt`

`config.txt`:

```
port=8080
host=localhost
debug=false
```

---

## Stage 1 — The `satisfy` primitive

Goal: "match any char satisfying `pred`" — the seed of the whole grammar.

```cpp
#include <fp/parse.hpp>
#include <string_view>

template <class P>
fp::Parser<char> satisfy(P pred) {
    return fp::Parser<char>([pred](std::string_view s)
        -> fp::Result<std::pair<char, std::string_view>> {
        if (s.empty() || !pred(s.front()))
            return fp::err<std::pair<char, std::string_view>>("no match");
        return fp::ok(std::pair<char, std::string_view>{s.front(), s.substr(1)});
    });
}
```

**Verify:** compiles. (`satisfy` is `template`, so it's only checked when used.)

**Concept — a parser is a value.** `Parser<T>` is
`std::function<Result<pair<T, string_view>>(string_view)>`. `satisfy` returns
one; everything below combines them.

## Stage 2 — `letter` and `alnum` classes

Goal: character classes from `satisfy`.

```cpp
#include <cctype>
auto letter = satisfy([](char c) { return std::isalpha((unsigned char)c); });
auto alnum  = satisfy([](char c) { return std::isalnum((unsigned char)c); });
```

**Verify:** `fp::run(letter, "a")` is `ok('a')`, `fp::run(letter, "1")` fails.

**Concept — composition over enumeration.** One `satisfy` gives you *any* class —
no `char_` per character.

## Stage 3 — `word` and `token` (a run of a class)

Goal: `some(p)` repeats a parser; `map` collects the result into a string.

```cpp
auto word  = fp::map(fp::some(letter), [](std::vector<char> const& cs) { return std::string(cs.begin(), cs.end()); });
auto token = fp::map(fp::some(alnum),  [](std::vector<char> const& cs) { return std::string(cs.begin(), cs.end()); });
```

**Verify:** `fp::run(word, "port")` is `ok("port")`; `fp::run(token, "8080")` is `ok("8080")`.

**Concept — `map` on a parser.** Succeed where the parser succeeds, transform
the value — the same `map` idea as `Result`, lifted to functions.

## Stage 4 — A `key=value` pair

Goal: parse `key`, remember it, parse `=`, parse `value`, pair them.

```cpp
auto pair = fp::and_then(word, [](std::string k) {
    return fp::map(fp::and_then(fp::string_("="), [](std::string) { return token; }),
                   [k](std::string v) { return std::pair{k, v}; });
});
```

**Verify:** `fp::run(pair, "port=8080")` is `ok({"port", "8080"})`.

**Concept — `and_then` threads context.** The key `k` is captured and used after
the value is parsed — the parser analogue of `>>=`.

## Stage 5 — A whole file: pairs separated by newlines

Goal: `sep_by` turns "one or more `pair`, separated by `sep`" into a parser.

```cpp
auto config = fp::sep_by(pair, fp::string_("\n"));
```

**Verify:** `fp::run(config, "port=8080\nhost=localhost")` is `ok({{"port","8080"},{"host","localhost"}})`.

**Concept — a grammar is composition.** Each combinator is one rule; the whole
file is `sep_by(pair, newline)`. No cursor, no loop.

## Stage 6 — Comments and blank lines

Goal: tolerate `# comment` and empty lines. The simplest correct way is to
*preprocess* the text — strip comments and blanks — before parsing:

```cpp
#include <fp/string.hpp>

std::string strip_comments(std::string const& text) {
    std::vector<std::string> keep;
    for (auto const& line : fp::str::lines(text)) {
        auto t = fp::str::trim(line);
        if (t.empty() || fp::str::starts_with(t, "#")) continue;
        keep.push_back(line);
    }
    return fp::str::join(keep, "\n");
}

// then: auto r = fp::run(config, strip_comments(text));
```

**Verify:** a config with `# comment` lines and blank lines parses the same as
the clean version.

**Concept — separate concerns.** Parsing the *grammar* and tolerating the *noise*
are two jobs; doing the noise in a pure preprocessing step keeps the parser
itself simple. (Doing it *inside* the parser with `alt`/`optional` is a harder
but worthwhile exercise — see the extensions.)

## Stage 7 — Sections

Goal: group pairs under `[section]` headers.

```cpp
auto section = fp::map(fp::and_then(fp::string_("["), [](std::string) {
    return fp::and_then(word, [](std::string name) {
        return fp::map(fp::string_("]"), [name](std::string) { return name; });
    });
}), [](auto s) { return s; });
```

**Verify:** parse `[server]\nport=8080` and confirm you can remember the current
section name (thread it through `and_then`).

**Concept — context again.** Sections are context that applies to the pairs that
follow — `and_then` carries it.

## Stage 8 — Validate: types

Goal: parsing says "well-formed"; now check *meaning*. `port` must be an int,
`debug` must be `true`/`false`.

```cpp
#include <fp/all.hpp>

fp::Validation<int> require_int(std::string const& key, std::string const& v) {
    auto r = fp::str::to_int(v);
    return r.is_ok() ? fp::valid(r.value()) : fp::invalid(key + " must be an integer");
}
```

**Verify:** `require_int("port", "abc")` has `error() == {"port must be an integer"}`.

**Concept — accumulate errors.** `Validation`'s error side is a
`vector<string>`; `combine` collects *every* failure, unlike `Result`'s first.

## Stage 9 — Validate: required keys

Goal: report missing keys.

```cpp
auto m = fp::to_map<std::string, std::string>(pairs);   // vector<pair> -> map
for (auto const& key : {"port", "host", "debug"})
    if (!fp::lookup(m, std::string(key)))
        /* collect "missing key" error */;
```

**Verify:** a config missing `port` reports `"missing key port"`.

**Concept — maps compose with validation.** `to_map`/`lookup` (from `map.hpp`)
turn the parsed pairs into a queryable structure; absence becomes a `Validation`
error.

## Stage 10 — Typed accessors

Goal: a clean `Config` object with `get_int`/`get_string`/`get_bool`.

```cpp
struct Config {
    std::map<std::string, std::string> values;
    fp::Result<int> get_int(std::string const& k) const {
        auto it = fp::lookup(values, k);
        return it ? fp::str::to_int(*it) : fp::err<int>("missing " + k);
    }
};
```

**Verify:** `cfg.get_int("port")` is `ok(8080)`; `cfg.get_int("nope")` is an error.

**Concept — a value-level API.** The parser produces plain data; the accessors
add typed, `Result`-returning queries on top. Clean separation.

---

## 🏆 Extensions

1. **Quoted values** — support `key="hello world"` (parse the `"..."` literal).
2. **Array values** — `ports=1,2,3` → `vector<int>` (reuse `sep_by` inside the value).
3. **Duplicate-key detection** — report "duplicate key `port`" instead of
   silently overwriting (check before `emplace`).
4. **Environment-variable interpolation** — `${HOME}` in a value expands from
   the process env (parse the `${...}` then look up).
5. **Round-trip** — write the validated `Config` back out (`fp::io::write_file`
   + `fp::str::join`).

Make it parse *your* config format, and make the errors good enough that a
user can fix their file from the message alone.
