# Parsing — `parse.hpp`

Parser combinators: build a parser from small parsers, like `parsec`/`nom` for
C++. A parser consumes a `std::string_view` and returns either the parsed value
plus the *leftover* input, or an error.

```cpp
#include <fp/parse.hpp>
```

`fp::Parser<T>` is a small struct wrapping a
`std::function<PResult<T>(std::string_view, std::size_t)>`, where
`PResult<T>` is `Either<ParseError, std::pair<T, std::string_view>>` and
`ParseError` carries a `message` and the input `offset` where things went
wrong. It's callable, constructs implicitly from a lambda taking
`(std::string_view, std::size_t)`, and (because it's a real type) carries
operators (`>>`, `<<`, `|`, `>>=`, `*`, `%`).

## The one idea

A parser is a function: `input → Result<(value, leftover)>`. It eats a *prefix*
of the input and returns **what it found** and **what's left**.

```cpp
fp::run(fp::char_('a'), "abc")   // ok('a')  — char_ matched 'a', left "bc"
```

Everything else is *combining* these functions.

## Primitives

| Function | Matches |
|---|---|
| `char_(c)` | one specific character |
| `string_(s)` | a literal string |
| `satisfy(pred)` | one char where `pred(c)` is true |
| `digit` / `letter` / `alnum` / `space` | character classes |
| `one_of('a','b',…)` / `none_of(…)` | any / none of a set |
| `eof` | end of input (consumes nothing) |
| `peek(p)` | run `p` without consuming; succeed only if it matches |
| `not_followed(p)` | negative lookahead: succeed iff `p` does **not** match |

```cpp
fp::digit;                       // a Parser<char>
fp::one_of('+', '-');            // '+' or '-'
fp::satisfy([](char c) { return c >= 'A' && c <= 'Z'; });
fp::not_followed(fp::digit) >> fp::letter;   // a letter that isn't a digit
```

## Sequencing

| Function | Result |
|---|---|
| `map(p, f)` | transform the parsed value |
| `and_then(p, f)` | `f(value)` returns the *next* parser (context/bind) |
| `seq(a, b)` | parse both, return `pair{a, b}` |
| `preceded(a, b)` | parse `a` then `b`, keep **b** |
| `terminated(a, b)` | parse `a` then `b`, keep **a** |
| `between(open, close, p)` | parse `open`, `p`, `close` — keep `p` |

```cpp
// "key: value"  ->  pair<string, int>,  skipping whitespace + ':'
auto pair = fp::seq(
    fp::lexeme(some(fp::letter)),                       // key
    fp::preceded(fp::symbol(':'), fp::lexeme(some(fp::digit))));
fp::run(pair, "width: 42");   // ok({"width", {'4','2'}})
```

`and_then` is the one you reach for when the next step *depends on* the value
(the parser analogue of `>>=`); `seq`/`preceded`/`terminated` cover the common
"parse this, then that" cases without a lambda.

## Operators

The same combinators have operator spellings (left = function form):

| Operator | Function | Meaning |
|---|---|---|
| `a >> b` | `preceded(a, b)` | parse `a` then `b`, keep **b** |
| `a << b` | `terminated(a, b)` | parse `a` then `b`, keep **a** |
| `a \| b` | `alt(a, b)` | try `a`, else `b` |
| `a >>= f` | `and_then(a, f)` | parse `a`, then run `f(value)` (bind) |
| `*p` | `many(p)` | zero or more |
| `p % sep` | `sep_by(p, sep)` | one or more `p`, separated by `sep` |

```cpp
// "key: 42"  ->  int   (no lambdas, no captures)
auto digits_to_int = [](std::vector<char> cs) {
    return std::stoi(std::string(cs.begin(), cs.end()));
};
auto value_after = fp::lexeme(some(fp::letter)) >> fp::symbol(':')
                 >> fp::lexeme(map(some(fp::digit), digits_to_int));

// "a12"  or  "a345"   (choice, repetition)
auto a_then_digits = fp::char_('a') >> *fp::digit;

// [1, 2, 3]  — note % binds tighter than >> / <<
auto array = fp::symbol('[') >> (fp::lexeme(fp::digit) % fp::symbol(',')) << fp::symbol(']');
```

`>>=` has the usual bind semantics; `succeed(value)` is a parser that yields a
value without consuming (useful as the last step of a bind):

```cpp
auto member = key >>= [](std::string k) {
    return symbol(':') >> number >>= [k](int n) { return succeed(std::pair{k, n}); };
};
```

## Repetition and choice

| Function | Result |
|---|---|
| `many(p)` / `some(p)` / `many1(p)` | zero+ / one+ / one+ |
| `sep_by(p, sep)` | one or more `p`, separated by `sep` |
| `optional(p)` | zero or one (never fails) |
| `alt(a, b)` / `choice(a, b, …)` | try each in order (backtracking) |
| `chainl1(p, op)` | left-associative fold: `p (op p)*` |

```cpp
fp::choice(fp::keyword("true"), fp::keyword("false"), fp::keyword("null"));

fp::sep_by(fp::lexeme(fp::digit), fp::symbol(','));   // "1, 2, 3"

// 1 + 2 + 3  ->  6   (`op` yields a binary callable)
auto digit_int = fp::map(fp::lexeme(fp::digit), [](char c) { return c - '0'; });
auto plus_op = fp::map(fp::symbol('+'),
    [](char) { return std::function<int(int, int)>([](int a, int b) { return a + b; }); });
auto expr = fp::chainl1(digit_int, plus_op);
```

## Whitespace-aware lexing

`lexeme(p)` parses `p` and then eats trailing whitespace; `symbol(c)` and
`keyword(s)` are the lexeme'd forms of `char_`/`string_`. Use them to make the
grammar ignore insignificant whitespace without sprinkling `space` parsers.

```cpp
fp::symbol(',');            // ',' plus any whitespace after it
fp::keyword("true");        // "true" plus any whitespace after it
fp::lexeme(fp::digit);      // a digit plus any whitespace after it
```

## Recursion

A grammar that refers to itself (JSON, expressions) needs `ref`: declare the
parser, build the parsers that use it, then assign it. `ref(p)` defers the
reference to *parse time*, so you don't have to capture a not-yet-assigned
parser in a lambda.

```cpp
fp::Parser<Json> value;                       // 1. declare

auto arr = fp::between(fp::symbol('['), fp::symbol(']'),
                       fp::sep_by(fp::ref(value), fp::symbol(',')));   // 2. use ref
// ...

value = fp::choice(/* ... */);                // 3. assign last
```

## Worked example: JSON

```cpp
#include <fp/all.hpp>
#include <variant>
#include <vector>

struct JsonNull {};
struct Json {
    using Array  = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;
    std::variant<JsonNull, bool, double, std::string, Array, Object> v;
};

int main() {
    using namespace fp;

    Parser<std::string> json_string = lexeme(map(
        between(char_('"'), char_('"'), many(none_of('"'))),
        [](std::vector<char> cs) { return std::string(cs.begin(), cs.end()); }));

    Parser<double> json_number = lexeme(map(
        some(satisfy([](char c) {
            return std::isdigit((unsigned char)c) || c=='-' || c=='.' || c=='e' || c=='E';
        })),
        [](std::vector<char> cs) { return std::stod(std::string(cs.begin(), cs.end())); }));

    auto jtrue  = map(keyword("true"),  [](auto) { return Json{true}; });
    auto jfalse = map(keyword("false"), [](auto) { return Json{false}; });
    auto jnull  = map(keyword("null"),  [](auto) { return Json{JsonNull{}}; });
    auto jstr   = map(json_string, [](std::string s) { return Json{s}; });
    auto jnum   = map(json_number, [](double d) { return Json{d}; });

    Parser<Json> value;                                   // recursion
    auto jarray = map(symbol('[') >> ref(value) % symbol(',') << symbol(']'),
                      [](Json::Array xs) { return Json{xs}; });
    auto member = seq(json_string, symbol(':') >> ref(value));  // pair<string,Json>
    auto jobject = map(symbol('{') >> member % symbol(',') << symbol('}'),
                       [](Json::Object ps) { return Json{ps}; });

    value = preceded(whitespace(), choice(jnull, jtrue, jfalse,
                                          jnum, jstr, jarray, jobject));

    auto r = run(value, R"({"name": "ada", "tags": [1, 2, 3], "ok": true})");
    // r.is_ok() (inspecting the tree is a `match` over `v`)
}
```

The whole grammar is primitives + sequencing + `choice` + one `ref` for the
recursion — no cursor, no state machine.

## Errors

`run(p, input)` returns `Result<T>` with the parser's message on failure, so
parse errors compose with the rest of the library. Failures carry the input
`offset` where they occurred; `run` renders it as a line/column location:

```cpp
run(list, "1,,2");   // err("line 1, col 3: expected item after separator")
```

Attach human context to the low-level failures with `label(p, "…")` (prefixes
one parser's message) or `context(p, "…")` (names the construct being parsed):

```cpp
auto item = fp::label(fp::lexeme(fp::digit), "an integer");
fp::run(item, "x");   // err("line 1, col 1: an integer")
```

The primitives (`char_`/`string_`/`satisfy`) are all you need to define new
building blocks; the rest is composition.
