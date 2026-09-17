# 07 — Parsing

**Headers:** `<fp/parse.hpp>` (`char_`, `string_`, `many`, `some`, `sep_by`,
`optional`, `map`, `and_then`, `alt`, `run`).

## What this module is about

A parser is a function `std::string_view -> Result<pair<T, string_view>>`: it
consumes some input and returns the parsed value *plus the leftover input*, or
an error. Parser combinators let you build a grammar by *combining* small
parsers, the way you build `Result`s with `and_then`. The two workhorses:

- `map` — succeed where `p` succeeds, transforming the value.
- `and_then` — receive the value, then decide which parser runs next (this is
  how context flows through a grammar).
- `alt` — backtrack and try another parser on failure.

`run(p, input)` runs to completion and returns `Result<T>`.

---

### 1. Match a literal · Easy

`string_("hello")` on `"hello world"` → `ok("hello")`.

**Why `string_`:** the base case of any grammar — "expect exactly this text".
It returns the matched literal and the leftover (`" world"`), which is what
lets later combinators continue where it stopped.

```cpp
auto p = fp::string_("hello");
auto r = fp::run(p, "hello world");
assert(r.is_ok() && r.value() == "hello");
```

### 2. One or more `a`s · Easy

`some(char_('a'))` on `"aaab"` → 3 `a`s.

**Why `some` vs `many`:** both repeat; `some` requires at least one match
(empty input is an error), `many` allows zero. Choosing the right one encodes
your grammar's intent in the type.

```cpp
auto p = fp::some(fp::char_('a'));
auto r = fp::run(p, "aaab");
assert(r.is_ok() && r.value().size() == 3);
```

### 3. Comma-separated words · Medium

Parse `"aa,aaa,aaaa"` into the word lengths `{2,3,4}`.

**Why compose `some` + `map` + `sep_by`:** three concerns, three combinators —
"one or more a's" (`some`), "turn that into a length" (`map`), "separated by
commas" (`sep_by`). Each piece is independently readable and testable; the
grammar falls out of composition.

```cpp
auto word_len = fp::map(fp::some(fp::char_('a')),
                        [](std::vector<char> const& cs){ return cs.size(); });
auto list = fp::sep_by(word_len, fp::char_(','));
auto r = fp::run(list, "aa,aaa,aaaa");
assert(r.is_ok() && r.value() == std::vector<size_t>({2,3,4}));
```

### 4. Optional section · Medium

A parser that always succeeds, capturing the value or `nullopt`.

**Why `optional`:** some grammar parts are allowed to be absent. `optional(p)`
never fails — if `p` matches, you get `some(value)`; if not, `nullopt` with the
input unchanged. It's the parser version of `Maybe`.

```cpp
auto word = fp::map(fp::some(fp::char_('a')), [](auto cs){ return std::string(cs.begin(), cs.end()); });
auto mid  = fp::optional(word);
assert(fp::run(mid, "aaa").value() == std::optional(std::string("aaa")));
assert(fp::run(mid, "bbb").value() == std::nullopt);
```

### 5. `key=value` (hard) · Hard

Parse `"aa=aaa"` into a `pair<string,string>`.

**Why `and_then` threads context:** after parsing the key, you need to *remember
it* while parsing the value. `and_then` hands the key to the next stage, which
parses `=` then the value, then pairs them up. This is the parser analogue of
`Result::and_then` — bind — and the single most important combinator for
grammars with context. (Note the lambdas capture the `word` parser so it can be
reused after the `=`.)

```cpp
Parser<std::string> word = fp::map(fp::some(fp::char_('a')),
                                   [](auto cs){ return std::string(cs.begin(), cs.end()); });
auto kv = fp::and_then(word, [word](std::string k) {
    return fp::map(fp::and_then(fp::char_('='), [word](char){ return word; }),
                   [k](std::string v){ return std::pair{k, v}; });
});
auto r = fp::run(kv, "aa=aaa");
assert(r.is_ok() && r.value() == std::pair(std::string("aa"), std::string("aaa")));
```

---

## Solutions

<details>
<summary>1. Match a literal</summary>

```cpp
auto p = fp::string_("hello");
auto r = fp::run(p, input);
```
</details>

<details>
<summary>2. One or more a's</summary>

```cpp
auto p = fp::some(fp::char_('a'));
auto r = fp::run(p, "aaab");   // r.value() == {'a','a','a'}
```
</details>

<details>
<summary>3. Comma-separated words</summary>

```cpp
auto word_len = fp::map(fp::some(fp::char_('a')),
                        [](std::vector<char> const& cs){ return cs.size(); });
auto list = fp::sep_by(word_len, fp::char_(','));
auto r = fp::run(list, input);
```
</details>

<details>
<summary>4. Optional section</summary>

```cpp
auto mid = fp::optional(word);
```
</details>

<details>
<summary>5. key=value</summary>

```cpp
auto kv = fp::and_then(word, [word](std::string k) {
    return fp::map(fp::and_then(fp::char_('='), [word](char){ return word; }),
                   [k](std::string v){ return std::pair{k, v}; });
});
```
</details>
