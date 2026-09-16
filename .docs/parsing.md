# Parsing — `parse.hpp`

Parser combinators: build a parser from small parsers, like `parsec`/`nom` for
C++. A parser consumes a `std::string_view` and returns either the parsed value
plus the *leftover* input, or an error.

```cpp
#include <fp/parse.hpp>

template <class T>
using Parser = std::function<Result<std::pair<T, std::string_view>>(std::string_view)>;
```

`Result` is the failure carrier (see [ADTs](adts.md)): `ok({value, leftover})`
on success, `err("why")` on failure.

## Core combinators

| Function | Result |
|---|---|
| `char_(c)` | match one character |
| `string_(s)` | match a literal string |
| `many(p)` | zero or more |
| `some(p)` | one or more |
| `sep_by(p, sep)` | one or more `p`, separated by `sep` |
| `optional(p)` | zero or one, always succeeds |
| `map(p, f)` | transform the parsed value |
| `and_then(p, f)` | bind: `f(value)` returns the next parser |
| `alt(a, b)` | try `a`, else `b` (choice) |
| `run(p, input)` | run to completion → `Result<T>` |

## Example: a comma-separated list of words

```cpp
#include <fp/parse.hpp>
using namespace fp;

// a "word" = one or more 'a's (keep it tiny for the example)
Parser<std::vector<char>> word = some(char_('a'));

// convert the chars to a string
Parser<std::string> word_str = map(word, [](std::vector<char> const& cs) {
    return std::string(cs.begin(), cs.end());
});

// words separated by commas
Parser<std::vector<std::string>> list = sep_by(word_str, char_(','));

auto r = run(list, "aa,a,aaa");
// r == ok({"aa", "a", "aaa"})
```

## Example: `key=value` with context via `and_then`

`and_then` is how you remember what you've parsed so far and continue:

```cpp
using namespace fp;

Parser<std::string> key = map(some(char_('a')), [](auto cs) {
    return std::string(cs.begin(), cs.end());
});

Parser<std::pair<std::string, std::string>> kv =
    and_then(key, [key](std::string k) {
        // after the '=', parse another word and pair it with the remembered key
        return map(and_then(char_('='), [key](char) { return key; }),
                   [k](std::string v) { return std::pair{k, v}; });
    });

Parser<std::vector<std::pair<std::string,std::string>>> pairs = sep_by(kv, char_(','));

run(pairs, "aa=aaa,aaaa=aaaaa");
// ok({{"aa","aaa"},{"aaaa","aaaaa"}})
```

Note the `[key]` captures: the inner lambdas close over the `key` parser (a
copyable `std::function`) so they can reuse it after the `=`.

## How the combinators compose

- `map(p, f)` — succeed where `p` succeeds, applying `f` to the value.
- `and_then(p, f)` — `f` receives the value and returns a *parser*; parsing
  continues with it. This threads context (`"aa=…"` → remember `"aa"` → parse
  the value).
- `alt(a, b)` — try `a`; if it fails, backtrack and try `b`.
- `optional(p)` — never fails: `ok(nullopt)` if `p` fails (input unchanged).
- `sep_by(p, sep)` — one or more `p` separated by `sep`; a trailing separator
  is an error (there must be an item after each separator).
- `many`/`some` — greedy repetition; `many` also matches the empty input.

## Errors

`run` returns `Result<T>` with the parser's message on failure:

```cpp
run(list, "aa,,");   // err("expected item after separator")
run(word_str, "");   // err(...)
```

Parse errors are `Result` strings — no exceptions — so they compose with the
rest of the library (`>>=`, `map`, `context`, …). The primitive `char_`/`string_`
don't cover character classes; compose `alt(char_('a'), char_('b'))` chains, or
add a `satisfy(pred)` combinator of your own on top of these primitives.
