# Project 2 — Config Parser & Validator

Build a parser for a small `key=value` config file, then *validate* the result.
This project introduces **parser combinators** and **accumulating validation**.

**Modules:** `parse.hpp`, `validation.hpp`, `result.hpp`, `string.hpp`.
**Compile:** `g++ -std=c++20 -I src -o app app.cpp && ./app config.txt`

Create `config.txt`:

```
port=8080
host=localhost
debug=false
```

---

## Step 1 — A primitive parser (`satisfy`)

The library ships `char_`/`string_` (match a specific char/literal). The
one primitive it doesn't ship is "match any char satisfying a predicate" —
which is trivial to build and unlocks everything else. Add it yourself:

```cpp
#include <fp/parse.hpp>
#include <string_view>

// match any single char where pred(c) is true
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

**Concept — a parser is a value:** `Parser<T>` is
`std::function<Result<pair<T, string_view>>(string_view)>` — it consumes input
and returns the value *plus the leftover*. `satisfy` is the seed the whole
grammar grows from.

## Step 2 — Build `word` and `token` from `satisfy`

`some(p)` repeats a parser one-or-more times; `map` transforms the collected
result. Letters make an identifier; letters+digits make a value token.

```cpp
#include <fp/parse.hpp>
#include <cctype>
#include <iostream>

// (satisfy from step 1)

auto letter = satisfy([](char c) { return std::isalpha(static_cast<unsigned char>(c)); });
auto alnum  = satisfy([](char c) { return std::isalnum(static_cast<unsigned char>(c)); });

// "one or more letters/digits" -> a std::string
auto word  = fp::map(fp::some(letter), [](std::vector<char> const& cs) { return std::string(cs.begin(), cs.end()); });
auto token = fp::map(fp::some(alnum),  [](std::vector<char> const& cs) { return std::string(cs.begin(), cs.end()); });

int main() {
    auto r = fp::run(word, "port");
    std::cout << (r.is_ok() ? r.value() : "failed") << "\n";   // "port"
}
```

**Concept — composition over character classes:** instead of a `char_` per
letter, you build `satisfy(isalpha)` once and get every identifier for free via
`some` + `map`.

## Step 3 — `key=value` with context

`and_then` is bind: parse the key, *remember it*, parse the `=`, then parse the
value. The inner lambda closes over the `token` parser.

```cpp
#include <fp/parse.hpp>
#include <cctype>
#include <iostream>

// (satisfy, letter, alnum, word, token from steps 1-2)

auto pair = fp::and_then(word, [](std::string k) {
    return fp::map(fp::and_then(fp::string_("="), [](std::string) { return token; }),
                   [k](std::string v) { return std::pair{k, v}; });
});

int main() {
    auto r = fp::run(pair, "port=8080");
    if (r.is_ok())
        std::cout << r.value().first << " -> " << r.value().second << "\n";
}
```

**Concept — context flows through `and_then`:** the key is captured and used
*after* the value is parsed — the parser analogue of `Result`'s `>>=`.

## Step 4 — A full config: pairs separated by newlines

`sep_by` handles "one or more `pair`, separated by `sep`". Parse newline-
separated pairs into a `vector<pair<string,string>>`.

```cpp
#include <fp/parse.hpp>
#include <fp/io.hpp>
#include <cctype>
#include <iostream>

// (satisfy, letter, alnum, word, token, pair from steps 1-3)

auto config = fp::sep_by(pair, fp::string_("\n"));

int main() {
    auto text = fp::read_file("config.txt");
    if (!text.is_ok()) { std::cerr << text.error() << "\n"; return 1; }

    auto r = fp::run(config, text.value());
    if (!r.is_ok()) { std::cerr << "parse error: " << r.error() << "\n"; return 1; }
    for (auto& [k, v] : r.value())
        std::cout << k << " = " << v << "\n";
}
```

**Concept — a grammar is composition:** `sep_by(pair, newline)` *is* "a config
file". Each combinator is one grammar rule; no cursor, no `for` loop.

## Step 5 — Validate the config

Parsing says "this is well-formed". Now validate *meaning*: `port` must be a
number, `debug` must be `true`/`false`. Use `Validation` to collect **all**
errors at once.

```cpp
#include <fp/all.hpp>
#include <iostream>

fp::Validation<int> require_int(std::string const& key, std::string const& v) {
    auto r = fp::str::to_int(v);
    return r.is_ok() ? fp::valid(r.value()) : fp::invalid(key + " must be an integer");
}

int main() {
    // (pretend this came from the parser in step 4)
    std::vector<std::pair<std::string, std::string>> pairs = {
        {"port", "8080"}, {"host", "localhost"}, {"debug", "false"}};

    auto port  = require_int("port", pairs[0].second);
    auto host  = fp::check([](std::string const& h) { return !h.empty(); },
                           "host must not be empty", pairs[1].second);
    auto debug = fp::check([](std::string const& d) { return d == "true" || d == "false"; },
                           "debug must be true/false", pairs[2].second);

    auto validated = fp::combine(
        [](int p, std::string h, std::string d) { return std::make_tuple(p, h, d); },
        port, host, debug);

    if (!validated.is_ok()) {
        for (auto const& e : validated.error()) std::cerr << e << "\n";
        return 1;
    }
    std::cout << "config OK\n";
}
```

**Concept — accumulate, don't stop:** `Validation` collects every error
(`port must be an integer`, `debug must be true/false`, …) so the user sees them
all at once, unlike `Result` which reports the first.

---

## 🏆 Challenge

Extend the grammar and the validator:

1. **Richer identifiers** — allow `_` and `-` in keys (extend `alnum`/`letter`
   predicates), and allow `.` in values (so `3.14` parses).
2. **Comments and blank lines** — ignore `# comment` lines and empty lines
   (hint: parse an *optional* comment after each pair, and tolerate leading/
   trailing newlines).
3. **Sections** — support `[server]` sections that group the pairs that follow
   (hint: `and_then` to remember the current section name).
4. **Required keys** — report "missing key `port`" if a required key is absent
   (hint: after parsing, `to_map` the pairs, then `lookup` the required keys and
   build a `Validation` from what's missing).

**Hints:**
- `alt(a, b)` tries `a`, then backtracks to `b` on failure.
- `optional(p)` never fails — `ok(nullopt)` if `p` doesn't match.
- `fp::to_map<K,V>(pairs)` and `fp::lookup(m, k)` (from `map.hpp`) return
  `optional<V>`.

The goal is a parser + validator for *your* config format. Make it parse
something you'd actually use.
