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

## Stage 1 — The `satisfy` primitive (built in)

Goal: "match any char satisfying `pred`" — the seed of the whole grammar. It
ships in `parse.hpp`, along with ready-made character classes:

```cpp
#include <fp/parse.hpp>

// fp::satisfy(pred)   -> Parser<char>
// fp::digit, fp::letter, fp::alnum, fp::space   -> Parser<char>
```

**Verify:** `fp::run(fp::digit, "5")` is `ok('5')`; `fp::run(fp::digit, "x")` fails.

**Concept — a parser is a value.** `Parser<T>` is
`std::function<Result<pair<T, string_view>>(string_view)>`. `satisfy` is the
primitive; `digit`/`letter`/… are thin wrappers over it; everything below
*combines* them.

## Stage 2 — Character classes for free

Goal: identifiers are runs of letters; values are runs of letters/digits. Use
the built-in classes directly.

```cpp
using namespace fp;
// fp::letter, fp::alnum are already `Parser<char>`
```

**Verify:** `fp::run(fp::letter, "a")` is `ok('a')`; `fp::run(fp::letter, "1")` fails.

**Concept — composition over enumeration.** One primitive gives you *every*
class — no `char_` per character.

## Stage 3 — `word` and `token` (a run of a class)

Goal: `some(p)` repeats a parser; `map` collects the result into a string.

```cpp
using namespace fp;
auto to_string = [](std::vector<char> const& cs) { return std::string(cs.begin(), cs.end()); };
auto word  = map(some(letter), to_string);   // "port", "host"
auto token = map(some(alnum),  to_string);   // "8080", "false"
```

**Verify:** `fp::run(word, "port")` is `ok("port")`; `fp::run(token, "8080")` is `ok("8080")`.

**Concept — `map` on a parser.** Succeed where the parser succeeds, transform
the value — the same `map` idea as `Result`, lifted to functions.

## Stage 4 — A `key=value` pair

Goal: parse `key`, `=`, `value`, and pair them up. With `seq` + `preceded` this
is one line — no lambdas, no captures:

```cpp
using namespace fp;
auto pair = seq(word, preceded(char_('='), token));   // Parser<pair<string, string>>
```

- `preceded(char_('='), token)` = parse `=`, then the value, keep **value**.
- `seq(key, value)` = parse both, return `pair{key, value}`.

**Verify:** `fp::run(pair, "port=8080")` is `ok({"port", "8080"})`.

**Concept — no context, so no `and_then`.** `and_then` is for when the next step
*depends on* the value (JSON does — see below); here the two halves are
independent, so `seq`/`preceded` say it directly. (Note: for grammars where
whitespace is insignificant, `symbol(c)` and `lexeme(p)` are the
whitespace-eating versions — not needed here, since a config is line-based.)

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
using namespace fp;
auto section = between(char_('['), char_(']'), word);   // Parser<string>
```

**Verify:** `fp::run(section, "[server]")` is `ok("server")`.

**Concept — `between` is the bracketed form.** `between(open, close, p)` parses
`open`, then `p`, then `close`, keeping `p` — exactly the "delimited" shape you'd
otherwise write as two nested `and_then`s.

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
add typed, `Result`-returning queries on top. Clean separation. And once you
hold a `Result`, the pipe maps over it:

```cpp
// double the port, or propagate the error
fp::Result<int> twice = fp::out(fp::into(cfg.get_int("port")) | fp::times(2));
```

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
