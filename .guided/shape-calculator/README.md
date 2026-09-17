# Project 3 — Shape Calculator

Build a shape library where each shape is a *sum type* (`std::variant`), and all
operations (`area`, `perimeter`) are written with exhaustive **pattern
matching**. This project introduces **sum types** and **exhaustive dispatch**.

**Modules:** `adt.hpp`, `variant`, `vec.hpp`, `parse.hpp`.
**Compile:** `g++ -std=c++20 -I src -o app app.cpp && ./app`

---

## Step 1 — Model shapes as a sum type

A shape is "one of several types". That's exactly `std::variant`:

```cpp
#include <variant>

struct Circle { double radius; };
struct Rect   { double w, h; };

using Shape = std::variant<Circle, Rect>;
```

**Concept — sum type:** instead of a base class + `virtual area()`, each case is
a plain struct and the union is a `variant`. There's no inheritance; the
*operations* are written outside the types (below), which keeps the data dumb
and the logic centralized.

## Step 2 — Area and perimeter with `match`

`match(s, case_<T>(f)...)` dispatches on which alternative is held, and the
compiler *checks* you covered every case.

```cpp
#include <fp/adt.hpp>
#include <variant>

struct Circle { double radius; };
struct Rect   { double w, h; };
using Shape = std::variant<Circle, Rect>;

double area(Shape const& s) {
    return fp::match(s,
        fp::case_<Circle>([](auto c) { return 3.14159 * c.radius * c.radius; }),
        fp::case_<Rect>  ([](auto r) { return r.w * r.h; }));
}

double perimeter(Shape const& s) {
    return fp::match(s,
        fp::case_<Circle>([](auto c) { return 2 * 3.14159 * c.radius; }),
        fp::case_<Rect>  ([](auto r) { return 2 * (r.w + r.h); }));
}

#include <iostream>
int main() {
    Shape s = Circle{2.0};
    std::cout << "area=" << area(s) << " perimeter=" << perimeter(s) << "\n";
}
```

**Concept — exhaustive dispatch:** `match` is O(1) (a jump on the variant index),
and if you later add a `Triangle`, this code *stops compiling* until you add a
`case_<Triangle>` arm. The compiler enforces that every operation handles every
shape.

## Step 3 — A list of shapes, folded

Now process many shapes at once — compute the total area with `map` + `sum`.

```cpp
#include <fp/all.hpp>
#include <vector>
#include <iostream>

struct Circle { double radius; };
struct Rect   { double w, h; };
using Shape = std::variant<Circle, Rect>;

double area(Shape const& s) {
    return fp::match(s,
        fp::case_<Circle>([](auto c) { return 3.14159 * c.radius * c.radius; }),
        fp::case_<Rect>  ([](auto r) { return r.w * r.h; }));
}

int main() {
    std::vector<Shape> shapes = { Circle{2.0}, Rect{3.0, 4.0}, Circle{1.0} };
    auto areas = fp::map(shapes, area);
    double total = fp::sum(areas);
    std::cout << "total area = " << total << "\n";
}
```

**Concept — compose:** `area` is a pure function; `map(shapes, area)` lifts it
over the collection. Sum types + collections + `map` = the whole pattern.

## Step 4 — Parse shapes from text

Bring in parser combinators to read shapes like `circle 2.0` / `rect 3.0 4.0`
from a file, then dispatch. (Numbers need a `satisfy`-style digit parser; here's
a small one.)

```cpp
#include <fp/all.hpp>
#include <optional>
#include <iostream>

// a parser that matches any char satisfying `pred`
template <class P>
fp::Parser<char> satisfy(P pred) {
    return fp::Parser<char>([pred](std::string_view s) -> fp::Result<std::pair<char, std::string_view>> {
        if (s.empty() || !pred(s.front()))
            return fp::err<std::pair<char, std::string_view>>("no match");
        return fp::ok(std::pair<char, std::string_view>{s.front(), s.substr(1)});
    });
}

int main() {
    auto digit = satisfy([](char c) { return c >= '0' && c <= '9'; });
    auto number = fp::map(fp::some(digit), [](std::vector<char> const& cs) {
        return std::stod(std::string(cs.begin(), cs.end()));
    });

    auto r = fp::run(number, "3.0");
    // (dot isn't a digit yet — see the challenge)
    std::cout << (r.is_ok() ? "parsed" : "failed") << "\n";
}
```

**Concept — build primitives, then compose:** `satisfy` is the primitive you add
once; `some`/`map` turn it into a number parser. This is how real parser
libraries grow — a couple of primitives, then composition.

---

## 🏆 Challenge

1. **Add a `Triangle`** (`{a, b, c}` side lengths). Add `case_<Triangle>` arms
   to `area` (Heron's formula) and `perimeter`. Watch the compiler *force* you
   to update every `match` — that's exhaustiveness working.
2. **Parse decimals** — the `number` parser above can't parse `3.0` (the `.`
   isn't a digit). Extend `satisfy`/`some` to accept `0-9` and `.`, then parse a
   full shape line like `circle 2.0` or `rect 3.0 4.0` (hint: `and_then` the
   keyword into the right constructor).
3. **Scale all shapes** — write `scale(Shape, double)` that multiplies every
   dimension (for `Circle`, the radius; for `Rect`, both `w` and `h`), then
   `map` it over a list.
4. **Largest shape** — find the shape with the greatest area (hint: sort the
   shapes by area with `sort_by`, or fold a running maximum).

**Hints:**
- Heron's formula for a triangle with sides `a,b,c`: `s = (a+b+c)/2`,
  `area = sqrt(s(s-a)(s-b)(s-c))`.
- For parsing keywords, `string_("circle")` + `and_then` is the pattern.
- `sort_by(v, key_fn)` sorts by a projected key.

Make it a shape *library* you'd reuse — a clean `area`/`perimeter`/`scale` that
stays correct as you add shapes.
