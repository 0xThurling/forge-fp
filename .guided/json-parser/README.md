# Project 10 — JSON Parser (CodeCrafters-style)

Build a real JSON parser with the [ergonomic layer](../../.docs/parsing.md):
primitives, sequencing, `choice`, whitespace-aware lexemes, and — the one new
idea — **recursion** via `ref`. Each stage adds a piece and ends with a
**Verify** check.

**Modules:** `parse.hpp`, `adt.hpp`, `variant`.
**Compile:** `g++ -std=c++20 -I src -o app app.cpp && ./app`

---

## Stage 1 — The value model

Goal: a JSON value is one of: null, bool, number, string, array, object — and
arrays/objects contain values. That's a **recursive** variant.

```cpp
#include <variant>
#include <vector>

struct JsonNull {};
struct Json {
    using Array  = std::vector<Json>;                       // [ ... ]
    using Object = std::vector<std::pair<std::string, Json>>; // { "k": v, ... }
    std::variant<JsonNull, bool, double, std::string, Array, Object> v;
};
```

**Verify:** compiles. (`std::vector<Json>` inside `Json` is allowed — `vector`
supports an incomplete element type.)

**Concept — model first.** The grammar has to produce *this*; define it before
writing any parser.

## Stage 2 — Strings

Goal: `"..."` → the content. `between` + `none_of` + `map`.

```cpp
#include <fp/all.hpp>
using namespace fp;

Parser<std::string> json_string = lexeme(map(
    between(char_('"'), char_('"'), many(none_of('"'))),
    [](std::vector<char> cs) { return std::string(cs.begin(), cs.end()); }));
```

**Verify:** `fp::run(json_string, "\"ada\"")` is `ok("ada")`.

**Concept — `between` for delimited things.** `between(open, close, p)` parses
`open`, then `p`, then `close`, keeping `p`. `lexeme(...)` eats trailing
whitespace so the next token starts clean. (No escapes yet — that's a challenge.)

## Stage 3 — Numbers and literals

Goal: `42` / `3.14` / `true` / `false` / `null` as `Json`.

```cpp
Parser<double> json_number = lexeme(map(
    some(satisfy([](char c) {
        return std::isdigit((unsigned char)c) || c=='-' || c=='+' || c=='.' || c=='e' || c=='E';
    })),
    [](std::vector<char> cs) { return std::stod(std::string(cs.begin(), cs.end())); }));

auto jnum   = map(json_number, [](double d) { return Json{d}; });
auto jtrue  = map(keyword("true"),  [](auto) { return Json{true}; });
auto jfalse = map(keyword("false"), [](auto) { return Json{false}; });
auto jnull  = map(keyword("null"),  [](auto) { return Json{JsonNull{}}; });
```

**Verify:** `fp::run(jnum, "3.14")` is `ok(Json{3.14})`; `fp::run(jtrue, "true")` is `ok(Json{true})`.

**Concept — `keyword` = literal + whitespace.** Same idea as `symbol`, for
multi-char tokens. All the scalars are now `Parser<Json>`.

## Stage 4 — Recursion: arrays

Goal: `[ … ]` of values. The array contains *values*, and a value can be an
array — so the value parser refers to itself.

```cpp
Parser<Json> value;                        // (a) declare the recursive parser

auto json_array = map(
    between(symbol('['), symbol(']'), sep_by(ref(value), symbol(','))),
    [](Json::Array xs) { return Json{xs}; });
```

**Verify:** once `value` is assigned in stage 6, `fp::run(value, "[1, 2, 3]")` works.

**Concept — `ref` defers the reference.** `ref(value)` returns a parser that
calls `value` *at parse time*. So you can build `json_array` before `value` is
assigned, and no lambda-capture gymnastics are needed.

## Stage 5 — Objects

Goal: `{ "k": v, … }`. A member is a key and a value; `seq` pairs them without a
lambda.

```cpp
auto member = seq(json_string, preceded(symbol(':'), ref(value)));  // pair<string, Json>

auto json_object = map(
    between(symbol('{'), symbol('}'), sep_by(member, symbol(','))),
    [](Json::Object ps) { return Json{ps}; });
```

**Verify:** once `value` is assigned, `fp::run(value, "{\"a\": 1}")` works.

**Concept — `seq` builds pairs.** `seq(key, value)` parses both and returns
`pair{key, value}` — exactly a member. `preceded(symbol(':'), value)` parses `:`
and keeps the value.

## Stage 6 — The value, and run it

Goal: `value` = try each alternative in order. Assign it **last**.

```cpp
value = preceded(whitespace(), choice(jnull, jtrue, jfalse,
                                      jnum, /* jstr, */ json_array, json_object));
```

Add the string alternative (`map(json_string, [](std::string s){ return Json{s}; })`)
and run:

```cpp
auto r = fp::run(value, R"({"name": "ada", "tags": [1, 2, 3], "ok": true, "x": null})");
```

**Verify:** `r.is_ok()`; `std::get<Json::Object>(r.value().v)` has 4 members.

**Concept — `choice` is variadic `alt`.** `preceded(whitespace(), …)` skips
leading whitespace at every value, and `symbol`/`keyword`/`lexeme` handle it
between tokens.

## Stage 7 — Inspect the tree with `match`

Goal: walk the parsed value using `match` (the ADT half of the library).

```cpp
std::string describe(Json const& j) {
    return fp::match(j.v,
        fp::case_<JsonNull> ([](auto) { return std::string("null"); }),
        fp::case_<bool>     ([](auto b) { return b ? "true" : "false"; }),
        fp::case_<double>   ([](auto d) { return std::to_string(d); }),
        fp::case_<std::string>([](auto const& s) { return "\"" + s + "\""; }),
        fp::case_<Json::Array> ([](auto const& a) { return "array[" + std::to_string(a.size()) + "]"; }),
        fp::case_<Json::Object>([](auto const& o) { return "object{" + std::to_string(o.size()) + "}"; }));
}
```

**Verify:** `describe(parsed)` reports the right kind for each member.

**Concept — parsing and consuming are two halves.** The parser builds the
variant; `match` (exhaustively, compiler-checked) consumes it.

---

## 🏆 Extensions

1. **String escapes** — handle `\"`, `\\`, `\n`, `\t`, `\uXXXX` (a small escape
   parser + `between`).
2. **Stricter numbers** — a real JSON number grammar (optional `-`, int, frac,
   exp) instead of the permissive `stod` shortcut.
3. **Pretty-print** — a `render(Json, indent)` that walks the tree and emits
   formatted text (recursion + `str::pad_left`).
4. **Error messages with position** — track the consumed offset and report
   `"line 3, col 12: expected ..."`.
5. **A small query language** — `get<Json>("a.b[0]")` to index into the tree.

The goal: a JSON parser where the *entire* structure is small parsers combined
with `seq`/`between`/`choice`/`sep_by` and exactly **one** recursive knot tied
with `ref` — no cursor, no state machine.
