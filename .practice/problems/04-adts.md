# 04 — ADTs: `Result`, `Either`, `Maybe`, `Validation`

**Headers:** `<fp/result.hpp>`, `<fp/maybe.hpp>`, `<fp/validation.hpp>`.

## What this module is about

The single most important idea in the library: model "a value **or** a failure"
as a *type*, then chain operations with `map` / `and_then` / `>>=` without
writing a single `if (failed)`. Errors are `std::string` messages carried as
values — nothing throws unless you `unwrap`. `Result` stops at the first error;
`Validation` collects *all* of them. `Maybe` is the optional-value version.

The mental shift: instead of
`if (!r) return err(...);` scattered everywhere, you write
`r >>= step1 >>= step2`, and the plumbing is handled for you.

---

### 1. Safe division · Easy

Write `div(a, b)` returning `Result<int>` — `err("div by zero")` when `b == 0`.

**Why:** division is the canonical *partial* function. Returning `Result` makes
the failure part of the type, so callers can't forget to handle it. Construct
with `fp::ok(v)` / `fp::err<T>("why")`.

```cpp
assert(div(10, 2).is_ok() && div(10, 2).value() == 5);
assert(!div(1, 0).is_ok() && div(1, 0).error() == "div by zero");
```

### 2. Double a parsed number · Easy

Parse a string to `int`, then double it — one `Result<int>` pipeline.

**Why `map` on `Result`:** `map` reaches *inside* the wrapper: transform the
value if present, propagate the error if not. You wrote no branch — `to_int`
failing just falls through.

```cpp
auto r = fp::map(fp::str::to_int("21"), [](int x){ return x*2; });
assert(r.value() == 42);
```

### 3. Chain three fallible steps · Medium

Parse → require positive → double, where *each* step can fail.

**Why `>>=` (`and_then`):** `map` is for a step that always succeeds; `and_then`
is for a step that can *also fail* (it returns a new `Result`). Chaining with
`>>=` flattens what would otherwise be nested `if`s into a straight line — this
is do-notation.

```cpp
assert(steps("5").value() == 10);
assert(!steps("-5").is_ok());
```

### 4. Safe head · Easy

Return the first element of a vector, or nothing if empty.

**Why `optional`:** "first element" doesn't exist for an empty vector. An
`optional<int>` is honest about that — no `-1`/`nullptr`/throw. `fp::head` is
exactly this.

```cpp
assert(first_of({3, 4}) == std::optional<int>(3));
assert(first_of({}) == std::nullopt);
```

### 5. Collect the present ones · Easy

`vector<optional<int>>` → `optional<vector<int>>`, all-or-nothing.

**Why `collect`:** "all present" is a whole-vector property. `collect` hoists a
bunch of optionals into one optional vector, so a single `if (result)` covers
every element.

```cpp
std::vector<std::optional<int>> all = {1, 2, 3};
assert(fp::collect(all) == std::optional(std::vector<int>{1,2,3}));
```

### 6. Validate a signup form · Medium

Check three fields and collect **all** errors — not just the first.

**Why `Validation`:** `Result` short-circuits at the first failure; a form wants
every problem reported at once. `Validation`'s error side is a
`std::vector<std::string>` and `combine2`/`combine` *accumulate* rather than
stop. This is the difference between "you have one error" and "here are your
three errors".

```cpp
auto v = signup("", 15, "nope");
assert(!v.is_ok() && v.error().size() == 3);   // all three errors present
```

### 7. Swap the layers · Medium

`Result<optional<int>>` ↔ `optional<Result<int>>`.

**Why `transpose`:** sometimes you hold a `Result` that might contain nothing,
and you'd rather hold a "maybe a `Result`". `transpose` flips the nesting so the
wrapper that matters to you is outermost — no manual unpacking.

```cpp
auto r = fp::ok(std::optional<int>{5});
auto o = fp::transpose(r);           // optional<Result<int>>
assert(o && o->value() == 5);
```

### 8. Wrap a throwing call · Easy

Turn `std::stoi` (which throws) into a `Result<int>`.

**Why `try_`:** interop with throwing code is where the error model meets
reality. `try_` catches `std::exception` (and `...`) and converts it to
`err(e.what())`, so one library boundary normalizes both worlds.

```cpp
auto r = fp::try_([&]{ return std::stoi("123"); });
assert(r.value() == 123);
```

### 9. All or nothing · Medium

`vector<Result<int>>` → `Result<vector<int>>`, first error wins.

**Why `sequence`:** the `Result` twin of `collect` — hoist many results into one
result. Short-circuits at the first error (unlike `Validation`), which is the
right default when one bad element invalidates the whole batch.

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
