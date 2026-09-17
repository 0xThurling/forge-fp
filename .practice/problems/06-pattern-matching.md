# 06 — Pattern matching

**Headers:** `<fp/adt.hpp>` (`match`, `case_`, `cond`, `when`, `otherwise`,
`overload`, `unpack`, `value_or`).

## What this module is about

`std::variant` holds "one of several types", but extracting that value is
normally a `std::visit` with a hand-written visitor, or `holds_alternative`
chains. `match`/`case_` make dispatch declarative and — crucially — *checked by
the compiler*: miss an alternative and it won't compile. `cond`/`when` are the
ordered-predicate sibling for when you want first-match guards instead of
exhaustive types.

---

### 1. Area of a shape · Easy

`area()` over `std::variant<Circle, Rect>`, using `match` + `case_`.

**Why `match` + `case_`:** dispatch is a jump on the variant index (O(1)), and
the compiler *enforces* that every alternative is handled — add a `Triangle`
later and this code stops compiling until you cover it. `case_<T>(f)` also
gives you a name, so the arm reads `case_<Circle>(...)` instead of
`[](Circle const& c){...}`.

```cpp
using Shape = std::variant<Circle, Rect>;
assert(area(Shape{Circle{2.0}}) > 12.5 && area(Shape{Circle{2.0}}) < 12.6);  // ~4π
assert(area(Shape{Rect{3.0, 4.0}}) == 12.0);
```

### 2. Describe a number · Easy

Return `"positive"`/`"negative"`/`"zero"` for an `int`, using `cond`.

**Why `cond` over `if`/`else`:** `cond(v, when(pred, f)..., otherwise(f))` is an
ordered, first-match-wins chain where every arm receives the value and the arms
are *data* (`when`/`otherwise` build them), not control flow. It reads as a
table of "if this predicate, produce that", and `otherwise` makes the fallback
explicit.

```cpp
assert(describe(5) == "positive");
assert(describe(-1) == "negative");
assert(describe(0) == "zero");
```

### 3. Unpack pairs into a fold · Medium

Zip `{1,2,3}` with `{10,20,30}`, sum the products (`140`).

**Why `unpack`:** `zip` produces pairs, but your function takes two arguments.
`unpack(f)` adapts `f(a, b)` to a pair-consuming callable, so you can write
`map(pairs, unpack(f))` instead of `map(pairs, [](auto p){ return f(p.first, p.second); })`
every time. Point-free glue for the library's pair-heavy combinators.

```cpp
auto products = fp::map(fp::zip(std::vector<int>{1,2,3}, std::vector<int>{10,20,30}),
                        fp::unpack([](int a, int b){ return a * b; }));
assert(fp::sum(products) == 140);
```

---

## Solutions

<details>
<summary>1. Area of a shape</summary>

```cpp
double area(Shape const& s) {
    return fp::match(s,
        fp::case_<Circle>([](auto c) { return 3.14159 * c.radius * c.radius; }),
        fp::case_<Rect>  ([](auto r) { return r.w * r.h; }));
}
```
</details>

<details>
<summary>2. Describe a number</summary>

```cpp
std::string describe(int x) {
    return fp::cond(x,
        fp::when(fp::gt(0),  [](auto) { return "positive"; }),
        fp::when(fp::lt(0),  [](auto) { return "negative"; }),
        fp::otherwise([](auto)      { return "zero"; }));
}
```
</details>

<details>
<summary>3. Unpack pairs into a fold</summary>

```cpp
auto products = fp::map(fp::zip(a, b), fp::unpack(fp::times));
int total = fp::sum(products);
```
</details>
