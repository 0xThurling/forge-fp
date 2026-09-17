# 03 — Strings

**Headers:** `<fp/string.hpp>` (namespace `fp::str`), `<fp/result.hpp>`.

## What this module is about

String processing is where C++'s default tools (`substr`, `find`, `getline`)
force the most index bookkeeping. `fp::str` wraps the common cases as pure
functions: `split`/`join`/`trim`/case helpers, and — importantly — `to_int`/
`to_double`, which return `Result` so a bad string becomes an *error value*
rather than an exception. That last bit is your first real look at the ADTs.

---

### 1. Word count · Easy

Count the words in `"the quick brown fox"` (4).

**Why `split`:** "break on a delimiter" is one call; `split(s, ' ').size()`
replaces the `find`-in-a-loop dance. Splitting returns a vector you can then
`map`/`filter`/`size` like any other collection.

```cpp
assert(fp::str::split("the quick brown fox", ' ').size() == 4);
```

### 2. Normalize a name · Easy

`"  Ada Lovelace  "` → `"ada lovelace"` (trim then lowercase).

**Why compose two pure transforms:** `to_lower(trim(s))` is a two-step
pipeline, each step a named operation. Because both are pure, you read
right-to-left: "trim, then lowercase" — no temp variable, no mutation.

```cpp
assert(fp::str::to_lower(fp::str::trim("  Ada Lovelace  ")) == "ada lovelace");
```

### 3. Join with a separator · Easy

`{"a","b","c"}` → `"a, b, c"`.

**Why `join`:** the inverse of `split`. The separator logic (add it *between*
items, not after the last) is a classic off-by-one that `join` gets right once.
The range overload also joins anything (ints, etc.), not just strings.

```cpp
assert(fp::str::join(std::vector<std::string>{"a","b","c"}, ", ") == "a, b, c");
```

### 4. Does it start with…? · Easy

Does `"hello.txt"` start with `"he"` and end with `".txt"`? (yes/yes)

**Why named predicates:** `starts_with`/`ends_with` read as questions, and
returning `bool` means they slot straight into `filter`/`cond`/`when`. The
manual `s.rfind(prefix, 0) == 0` is noise by comparison.

```cpp
assert(fp::str::starts_with("hello.txt", "he"));
assert(fp::str::ends_with("hello.txt", ".txt"));
```

### 5. Parse a CSV row of numbers · Medium

`"1,2,3,4"` → `std::vector<int>{1,2,3,4}`.

**Why `split` + `traverse`:** splitting gives strings; `traverse` maps each
through a *fallible* function (`to_int`) and combines the `Result`s — all
succeed → the vector, any fail → the error. This is the "parse a row" idiom in
two combinators, with failure handled as a value rather than a thrown exception.

```cpp
auto parts = fp::str::split("1,2,3,4", ',');
auto nums  = fp::traverse(parts, fp::str::to_int);
assert(nums.is_ok() && nums.value() == std::vector<int>({1,2,3,4}));
```

### 6. Sanitize and parse a number · Medium

`"  42  "` → `Result<int>` holding 42, then doubled to 84.

**Why `map` over a `Result`:** `to_int` already trims, but the point is to show
`map` working on the *wrapper*: it reaches inside the `Result`, applies
`times(2)` to the value, and passes the failure through untouched. Transforming
a value you may not have, without a single `if`.

```cpp
auto r = fp::map(fp::str::to_int(fp::str::trim("  42  ")), fp::times(2));
assert(r.is_ok() && r.value() == 84);
```

### 7. Title-case a headline · Medium

`"fp is great"` → `"Fp Is Great"`.

**Why a named function over the loop:** capitalizing each word is a stateful
walk (track "am I at a word start?"). `fp::str::title` hides that state machine
behind a name, so call sites stay declarative and the edge cases live in one
place.

```cpp
assert(fp::str::title("fp is great") == "Fp Is Great");
```

---

## Solutions

<details>
<summary>1. Word count</summary>

```cpp
size_t n = fp::str::split(s, ' ').size();
```
</details>

<details>
<summary>2. Normalize a name</summary>

```cpp
std::string out = fp::str::to_lower(fp::str::trim(s));
```
</details>

<details>
<summary>3. Join with a separator</summary>

```cpp
std::string out = fp::str::join(parts, ", ");
```
</details>

<details>
<summary>4. Does it start with…?</summary>

```cpp
bool a = fp::str::starts_with(s, "he");
bool b = fp::str::ends_with(s, ".txt");
```
</details>

<details>
<summary>5. Parse a CSV row of numbers</summary>

```cpp
auto parts = fp::str::split(s, ',');
auto nums  = fp::traverse(parts, fp::str::to_int);   // Result<vector<int>>
```
</details>

<details>
<summary>6. Sanitize and parse a number</summary>

```cpp
auto r = fp::map(fp::str::to_int(fp::str::trim(s)), fp::times(2));
```
</details>

<details>
<summary>7. Title-case a headline</summary>

```cpp
std::string out = fp::str::title(s);
```
</details>
