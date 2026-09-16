# 04 — ADTs: `Result`, `Either`, `Maybe`, `Validation`

**Headers:** `<fp/result.hpp>`, `<fp/maybe.hpp>`, `<fp/validation.hpp>`.

This is the heart of the library: model "a value or a failure" and chain it
with `map` / `and_then` / `>>=`. Errors are `std::string` messages; nothing
throws unless you `unwrap`.

---

### 1. Safe division · Easy

Write `div(int a, int b)` returning `Result<int>`: `err("div by zero")` when
`b == 0`, else `ok(a / b)`.

```cpp
assert(div(10, 2).is_ok() && div(10, 2).value() == 5);
assert(!div(1, 0).is_ok() && div(1, 0).error() == "div by zero");
```

### 2. Double a parsed number · Easy

Given a string, parse it to `int` and double it — the whole thing as one
`Result<int>` pipeline. Use `str::to_int` + `map`.

```cpp
auto r = fp::map(fp::str::to_int("21"), [](int x){ return x*2; });
assert(r.value() == 42);
```

### 3. Chain three fallible steps · Medium

Parse a string to `int`, then keep it only if it's positive, then double it.
Use `>>=` (or `and_then`) so each step can fail.

```cpp
// "5" -> ok(10); "-5" -> err("negative"); "x" -> err("not a number")
assert(steps("5").value() == 10);
assert(!steps("-5").is_ok());
```

### 4. Safe head · Easy

Write `first_of(vector<int>)` returning `optional<int>` — the first element,
or `nullopt` if empty. Use `fp::head`.

```cpp
assert(first_of({3, 4}) == std::optional<int>(3));
assert(first_of({}) == std::nullopt);
```

### 5. Collect the present ones · Easy

Given a vector of `optional<int>`, return `optional<vector<int>>` — all the
values if every one is present, else `nullopt`. Use `fp::collect`.

```cpp
std::vector<std::optional<int>> all = {1, 2, 3};
assert(fp::collect(all) == std::optional(std::vector<int>{1,2,3}));
```

### 6. Validate a signup form · Medium

Check three fields and collect **all** errors (not just the first). Use
`Validation` + `combine2` (or `combine`). Fields: name non-empty, age >= 18,
email contains `@`.

```cpp
auto v = signup("", 15, "nope");
assert(!v.is_ok() && v.error().size() == 3);   // all three errors present
```

### 7. Swap the layers · Medium

Given `Result<optional<int>>`, turn it into `optional<Result<int>>` (or the
other direction). Use `fp::transpose`.

```cpp
auto r = fp::ok(std::optional<int>{5});
auto o = fp::transpose(r);           // optional<Result<int>>
assert(o && o->value() == 5);
```

### 8. Wrap a throwing call · Easy

Given `std::stoi` (which throws), make a `Result<int>` out of it with `fp::try_`.

```cpp
auto r = fp::try_([&]{ return std::stoi("123"); });
assert(r.value() == 123);
```

### 9. All or nothing · Medium

Given a vector of `Result<int>`, return `Result<vector<int>>`: every value if
all succeed, else the first error. Use `fp::sequence`.

```cpp
std::vector<fp::Result<int>> ok_all = { fp::ok(1), fp::ok(2) };
assert(fp::sequence(ok_all).value() == std::vector<int>({1,2}));
```

---

## Solutions

<details>
<summary>1. Safe division</summary>

```cpp
fp::Result<int> div(int a, int b) {
    return b == 0 ? fp::err<int>("div by zero") : fp::ok(a / b);
}
```
</details>

<details>
<summary>2. Double a parsed number</summary>

```cpp
auto r = fp::map(fp::str::to_int(s), fp::times(2));
```
</details>

<details>
<summary>3. Chain three fallible steps</summary>

```cpp
fp::Result<int> steps(std::string const& s) {
    return fp::str::to_int(s)
        >>= [](int x) { return x > 0 ? fp::ok(x) : fp::err<int>("negative"); }
        >>= [](int x) { return fp::ok(x * 2); };
}
```
</details>

<details>
<summary>4. Safe head</summary>

```cpp
std::optional<int> first_of(std::vector<int> const& v) { return fp::head(v); }
```
</details>

<details>
<summary>5. Collect the present ones</summary>

```cpp
auto all = fp::collect(os);
```
</details>

<details>
<summary>6. Validate a signup form</summary>

```cpp
fp::Validation<User> signup(std::string name, int age, std::string email) {
    auto ok_name  = fp::check([](std::string const& n){ return !n.empty(); },
                               "name required", name);
    auto ok_age   = fp::check([](int a){ return a >= 18; }, "too young", age);
    auto ok_email = fp::check([](std::string const& e){ return e.find('@') != std::string::npos; },
                               "bad email", email);
    // combine2 accumulates errors from both sides; chain for three:
    return fp::combine2(ok_name, fp::combine2(ok_age, ok_email, [](int a, std::string e){
        return std::pair{a, e};
    }), [](std::string n, auto p){ return User{n, p.first, p.second}; });
}
```
</details>

<details>
<summary>7. Swap the layers</summary>

```cpp
auto o = fp::transpose(r);   // Result<optional<int>> -> optional<Result<int>>
```
</details>

<details>
<summary>8. Wrap a throwing call</summary>

```cpp
auto r = fp::try_([&]{ return std::stoi(s); });
```
</details>

<details>
<summary>9. All or nothing</summary>

```cpp
auto r = fp::sequence(results);
```
</details>
