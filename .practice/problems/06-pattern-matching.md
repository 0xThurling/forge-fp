# 06 — Pattern matching

**Headers:** `<fp/adt.hpp>` (`match`, `case_`, `cond`, `when`, `otherwise`,
`overload`, `unpack`, `value_or`).

Dispatch on `std::variant` exhaustively, or guard with ordered predicates.

---

### 1. Area of a shape · Easy

Given a `std::variant<Circle, Rect>` where `Circle{double radius}` and
`Rect{double w, h}`, write `area(shape)` using `match` + `case_`.

```cpp
using Shape = std::variant<Circle, Rect>;
assert(area(Shape{Circle{2.0}}) > 12.5 && area(Shape{Circle{2.0}}) < 12.6);  // ~4π
assert(area(Shape{Rect{3.0, 4.0}}) == 12.0);
```

### 2. Describe a number · Easy

Given an `int`, return `"positive"`, `"negative"`, or `"zero"` using `cond` +
`when`/`otherwise` (not `if`/`else`).

```cpp
assert(describe(5) == "positive");
assert(describe(-1) == "negative");
assert(describe(0) == "zero");
```

### 3. Unpack pairs into a fold · Medium

Given `{1,2,3}` and `{10,20,30}`, zip them and sum the products
(`1*10 + 2*20 + 3*30 = 140`). Use `zip` + `map` + `unpack`.

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
