# 03 — Strings

**Headers:** `<fp/string.hpp>` (namespace `fp::str`), `<fp/result.hpp>`.

`split`, `join`, `trim`, case helpers, and `to_int`/`to_double` (which return
`Result` — the first taste of the ADTs).

---

### 1. Word count · Easy

Given `"the quick brown fox"`, count the words (4). Use `split`.

```cpp
assert(fp::str::split("the quick brown fox", ' ').size() == 4);
```

### 2. Normalize a name · Easy

Given `"  Ada Lovelace  "`, produce `"ada lovelace"` — trim then lowercase.

```cpp
assert(fp::str::to_lower(fp::str::trim("  Ada Lovelace  ")) == "ada lovelace");
```

### 3. Join with a separator · Easy

Given `{"a","b","c"}`, produce `"a, b, c"`.

```cpp
assert(fp::str::join(std::vector<std::string>{"a","b","c"}, ", ") == "a, b, c");
```

### 4. Does it start with…? · Easy

Given `"hello.txt"`, answer whether it starts with `"he"` and ends with
`".txt"` (both true). Use `starts_with` / `ends_with`.

```cpp
assert(fp::str::starts_with("hello.txt", "he"));
assert(fp::str::ends_with("hello.txt", ".txt"));
```

### 5. Parse a CSV row of numbers · Medium

Given `"1,2,3,4"`, produce `std::vector<int>{1,2,3,4}`. Split on `,`, then
`traverse` the pieces with `to_int` and `unwrap` (or check the `Result`).

```cpp
auto parts = fp::str::split("1,2,3,4", ',');
auto nums  = fp::traverse(parts, fp::str::to_int);
assert(nums.is_ok() && nums.value() == std::vector<int>({1,2,3,4}));
```

### 6. Sanitize and parse a number · Medium

Given `"  42  "`, produce `Result<int>` holding 42. `to_int` already trims —
but use `trim` + `to_int` explicitly, then `map` it to double it (84).

```cpp
auto r = fp::map(fp::str::to_int(fp::str::trim("  42  ")), fp::times(2));
assert(r.is_ok() && r.value() == 84);
```

### 7. Title-case a headline · Medium

Given `"fp is great"`, produce `"Fp Is Great"` using `fp::str::title`.

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
