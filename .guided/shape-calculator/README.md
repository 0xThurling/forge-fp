# Project 3 — Shape Calculator (CodeCrafters-style)

Build a shape library where shapes are a **sum type** (`std::variant`) and every
operation is written with exhaustive `match`. Each stage adds a shape or an
operation and ends with a **Verify** check.

**Modules:** `adt.hpp`, `variant`, `vec.hpp`, `parse.hpp`.
**Compile:** `g++ -std=c++20 -I src -o app app.cpp && ./app`

---

## Stage 1 — Model a shape as a `variant`

Goal: a `Shape` is "one of several types".

```cpp
#include <variant>
struct Circle { double radius; };
using Shape = std::variant<Circle>;
```

**Verify:** `Shape s = Circle{2.0};` compiles.

**Concept — sum type.** No base class, no `virtual`. The cases are plain
structs; the union is the `variant`. Operations live *outside* the types.

## Stage 2 — `area` with `match`

Goal: dispatch on the alternative and return the area.

```cpp
#include <fp/adt.hpp>

double area(Shape const& s) {
    return fp::match(s,
        fp::case_<Circle>([](auto c) { return 3.14159 * c.radius * c.radius; }));
}
```

**Verify:** `area(Circle{2.0})` ≈ `12.566`.

**Concept — exhaustive dispatch.** `match` jumps on the variant index (O(1)).
Right now there's one arm; the value of exhaustiveness shows in the next stage.

## Stage 3 — Add `Rect` — the compiler forces the update

Goal: add `Rect{w, h}` and watch `area` break until you handle it.

```cpp
struct Rect { double w, h; };
using Shape = std::variant<Circle, Rect>;

double area(Shape const& s) {
    return fp::match(s,
        fp::case_<Circle>([](auto c) { return 3.14159 * c.radius * c.radius; }),
        fp::case_<Rect>  ([](auto r) { return r.w * r.h; }));
}
```

**Verify:** `area(Rect{3.0, 4.0}) == 12.0`.

**Concept — the compiler checks exhaustiveness.** If you forget the `Rect` arm,
it won't compile. That's the guarantee a `virtual`+`if` approach can't give.

## Stage 4 — `perimeter`

Goal: a second operation over the same sum type.

```cpp
double perimeter(Shape const& s) {
    return fp::match(s,
        fp::case_<Circle>([](auto c) { return 2 * 3.14159 * c.radius; }),
        fp::case_<Rect>  ([](auto r) { return 2 * (r.w + r.h); }));
}
```

**Verify:** `perimeter(Rect{3.0, 4.0}) == 14.0`.

**Concept — operations, not methods.** You add an operation by writing one more
`match`, not by touching the shape structs.

## Stage 5 — Add `Triangle` (Heron's formula)

Goal: a third shape, and both operations grow.

```cpp
#include <cmath>
struct Triangle { double a, b, c; };
using Shape = std::variant<Circle, Rect, Triangle>;

double area(Shape const& s) {
    return fp::match(s,
        fp::case_<Circle>  ([](auto c) { return 3.14159 * c.radius * c.radius; }),
        fp::case_<Rect>    ([](auto r) { return r.w * r.h; }),
        fp::case_<Triangle>([](auto t) {
            double s = (t.a + t.b + t.c) / 2.0;
            return std::sqrt(s * (s - t.a) * (s - t.b) * (s - t.c));
        }));
}
```

**Verify:** a 3-4-5 triangle has area `6.0`.

**Concept — exhaustive again.** Both `area` and `perimeter` now *must* handle
`Triangle`; the compiler walks you through the update.

## Stage 6 — A list of shapes, folded

Goal: many shapes → total area.

```cpp
std::vector<Shape> shapes = { Circle{2.0}, Rect{3.0, 4.0}, Triangle{3,4,5} };
double total = fp::sum(fp::map(shapes, area));
```

**Verify:** `total` ≈ `12.566 + 12 + 6 = 30.566`.

**Concept — compose.** `area` is pure; `map(shapes, area)` lifts it over the
collection, `sum` folds it. Sum types + collections + `map`.

## Stage 7 — `scale` a shape

Goal: multiply every dimension by a factor.

```cpp
Shape scale(Shape const& s, double k) {
    return fp::match(s,
        fp::case_<Circle>  ([k](Circle const& c)   { return Shape{Circle{c.radius * k}}; }),
        fp::case_<Rect>    ([k](Rect const& r)     { return Shape{Rect{r.w * k, r.h * k}}; }),
        fp::case_<Triangle>([k](Triangle const& t) { return Shape{Triangle{t.a * k, t.b * k, t.c * k}}; }));
}
```

**Verify:** `scale(Circle{2.0}, 3.0)` is a `Circle` with radius `6.0`.

**Concept — capture in the arm.** `case_<T>(f)` gives `f` the shape value; the
lambda captures `k` from the enclosing `scale`. Each arm scales its own fields.

## Stage 8 — Parse shapes from text

Goal: read `circle 2.0` / `rect 3.0 4.0` from a file. First a number parser:

```cpp
auto digit  = satisfy([](char c) { return c >= '0' && c <= '9'; });
auto number = fp::map(fp::some(digit), [](std::vector<char> const& cs) {
    return std::stod(std::string(cs.begin(), cs.end()));
});

auto circle = fp::map(fp::and_then(fp::string_("circle "), [number](std::string) { return number; }),
                      [](double r) { return Shape{Circle{r}}; });
```

**Verify:** `fp::run(circle, "circle 2.0")` is `ok(Circle{2.0})`.

**Concept — parse then construct.** `and_then` parses the keyword, then the
number, and `map` builds the value.

## Stage 9 — Largest shape

Goal: the shape with the greatest area.

```cpp
// sort by area, take the largest
auto by_area = fp::sort_by(shapes, area);
Shape biggest = by_area.back();
```

**Verify:** `biggest` is the `Circle` (area ~12.57).

**Concept — projection.** `sort_by(v, key_fn)` sorts by a projected key — here
`area`. The "max by area" question becomes a sort + `back()`.

## Stage 10 — Invalid shapes as `Result`

Goal: reject degenerate shapes (negative radius, triangle that violates the
inequality) with a `Result`.

```cpp
fp::Result<Shape> make_circle(double r) {
    return r > 0 ? fp::ok(Shape{Circle{r}}) : fp::err<Shape>("radius must be positive");
}

// and chain it with the pipe — `|` maps over the Result, propagating the error
fp::Result<double> area_of_circle(double r) {
    return fp::out(fp::into(make_circle(r)) | area);
}
```

**Verify:** `make_circle(-1)` is an error; `make_circle(2)` is `ok`, and
`area_of_circle(-1)` is an error while `area_of_circle(2)` ≈ `12.57`.

**Concept — validate at the boundary, then pipe.** The sum type is total (any
`Shape` is valid); you push *invalid* inputs out to a `Result` at the constructor
boundary. From there, `into(result) | area` maps `area` over the value —
short-circuiting on the error — so a `Result` composes like any other value in a
pipe.

---

## 🏆 Extensions

1. **A `Polygon` with any number of vertices** — area via the shoelace formula.
2. **`describe(shape)`** — a human-readable string using `match`.
3. **Bounding box** — compute each shape's axis-aligned bounding box.
4. **Translate** — move a shape by a `(dx, dy)` offset (mirrors `scale`).
5. **Serialize** — write shapes to text and read them back (combine stages 8–9).

The goal: a shape library where adding a shape *forces* every operation to
handle it, and where operations are pure functions you can `map`/`fold`.
