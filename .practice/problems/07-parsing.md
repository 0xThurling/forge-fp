# 07 — Parsing

**Headers:** `<fp/parse.hpp>` (`char_`, `string_`, `many`, `some`, `sep_by`,
`optional`, `map`, `and_then`, `alt`, `run`).

`Parser<T>` is `std::function<Result<std::pair<T, std::string_view>>(std::string_view)>`.
`run(p, input)` runs to completion and returns `Result<T>`.

---

### 1. Match a literal · Easy

Build a parser with `string_` that matches `"hello"` and use `run` on
`"hello world"` → `ok("hello")`.

```cpp
auto p = fp::string_("hello");
auto r = fp::run(p, "hello world");
assert(r.is_ok() && r.value() == "hello");
```

### 2. One or more `a`s · Easy

Build `some(char_('a'))` and count how many `a`s are at the start of `"aaab"`.

```cpp
auto p = fp::some(fp::char_('a'));
auto r = fp::run(p, "aaab");
assert(r.is_ok() && r.value().size() == 3);
```

### 3. Comma-separated words · Medium

Parse `"aa,aaa,aaaa"` into `vector<size_t>` of the word lengths
(`{2,3,4}`). Compose `some(char_('a'))` + `map` (to length) + `sep_by(char_(','))`.

```cpp
auto word_len = fp::map(fp::some(fp::char_('a')),
                        [](std::vector<char> const& cs){ return cs.size(); });
auto list = fp::sep_by(word_len, fp::char_(','));
auto r = fp::run(list, "aa,aaa,aaaa");
assert(r.is_ok() && r.value() == std::vector<size_t>({2,3,4}));
```

### 4. Optional section · Medium

A parser that *always* succeeds: capture the value when it's there, otherwise
`nullopt`. Use `optional` around a `word` parser.

```cpp
auto word = fp::map(fp::some(fp::char_('a')), [](auto cs){ return std::string(cs.begin(), cs.end()); });
auto mid  = fp::optional(word);
assert(fp::run(mid, "aaa").value() == std::optional(std::string("aaa")));
assert(fp::run(mid, "bbb").value() == std::nullopt);   // word fails, optional swallows it
```

### 5. `key=value` (hard) · Hard

Parse `"aa=aaa"` into a `pair<string,string>` using `and_then` to remember the
key while parsing the value. (Capture the `word` parser in the inner lambdas.)

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
