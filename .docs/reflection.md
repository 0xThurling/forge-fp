# Reflection — `reflect.hpp`

`reflect.hpp` adds dynamic reflection to ForgeFP: ask a type for its fields at
compile time, and reach those fields by name or index at runtime. It needs no
macros, no RTTI, and no external dependency.

```cpp
#include <fp/reflect.hpp>
```

**Why this exists:** reflection is normally either macro-heavy (`REFLECT(Type,
x, y, z)`) or compiler-specific. C++20 has enough machinery to do it for
aggregates without either: the field structure comes from aggregate
initialization and structured bindings, and the field *names* come from a
compile-time pointer spelling trick on GCC/Clang (the one Boost.PFR uses).
Where a compiler cannot spell names out, fields fall back to `field0`,
`field1`, … and everything else keeps working.

```cpp
struct Point { int x; float y; char tag; };

fp::reflect::arity<Point>();               // 3
fp::reflect::field_at<0>(p) = 42;          // typed access, no name needed
fp::reflect::field_name<Point, 1>();       // "y" (GCC/Clang)
fp::reflect::to_string(p);                 // "Point{x=42, y=1.5, tag='a'}"
```

## What is reflectable

| Type | Reflected? |
|---|---|
| Aggregate `struct`/`class` (public members, no user constructors) | yes, up to `max_fields` (32) |
| Nested aggregates | yes, recursively (each field keeps its real type) |
| C arrays | yes, element-wise (`arity<int[3]>() == 3`) |
| `std::array` / `std::pair` / `std::tuple` | yes, element-wise through the tuple protocol (index names) |
| `enum` | name/value mapping (see below) |
| Non-aggregate, union, aggregate with more than 32 fields | no — opt in with `FieldAccess` |

## Compile-time access

```cpp
struct Point { int x; float y; char tag; };
Point p{1, 2.0f, 'c'};

fp::reflect::arity<Point>();                    // 3
fp::reflect::has_fields<Point>();               // true
fp::reflect::field_at<1>(p);                    // float& -> 2.0f
fp::reflect::field_at<0>(std::as_const(p));     // const int&
fp::reflect::field_type<1, Point>;              // float

// visit every field; the lambda is instantiated once per field type
fp::reflect::for_each_field(p, [](std::size_t i, auto &value) {
  using T = std::remove_cvref_t<decltype(value)>;
  if constexpr (std::is_same_v<T, float>)
    value *= 2.0f;
});
```

`field_at` returns a reference, so it is both a reader and a writer. The index
is a template parameter, so the types are exact and no dispatch happens at
runtime. Because the visitor body is instantiated per field type, per-type code
uses `if constexpr` — that is also how you get compile-time dispatch over the
fields.

### Compile-time field queries

```cpp
static_assert(fp::reflect::has_field<Point>("x"));
static_assert(fp::reflect::field_index<Point>("tag") == 2);
static_assert(fp::reflect::field_index<Point>("nope") == fp::reflect::no_field);

if constexpr (fp::reflect::has_field<Shape>("position"))
  use_position(shape);          // generic code that adapts to the type
```

Both are `constexpr`, so they work in `if constexpr` with a literal name. At
runtime they are a loop over the field table.

## Metadata

```cpp
constexpr auto info = fp::reflect::describe<Point>();
// info.id, info.name, info.size, info.align, info.kind,
// info.trivially_copyable, info.standard_layout, info.aggregate
// info.fields -> span of FieldInfo { name, type, type_name, size }

fp::reflect::fields<Point>()[0].name;       // "x"
fp::reflect::fields<Point>()[0].type_name;  // "int"
```

`describe<T>()` is `constexpr` and never touches the registry. **Field names
are the expensive part of reflection at compile time** — see
[Compile-time cost](#compile-time-cost). If you only need sizes, kinds and
field types, use the cheap variant:

```cpp
constexpr auto shape = fp::reflect::describe_shape<Point>();
shape.fields[0].name;   // "field0" — names are never extracted
```

For a runtime descriptor (and to make T findable by name), use
`type_info<T>()`, which caches and registers it on first use:

```cpp
const auto &info = fp::reflect::type_info<Point>();   // registered as "Point"
fp::reflect::find_type("Point");                      // optional<TypeInfo>
fp::reflect::find_type_by_id(fp::reflect::type_id<Point>());
fp::reflect::types();                                 // everything registered
fp::reflect::register_type<Point>("Pt");              // extra alias
```

`TypeId` is a pointer to a per-type tag: comparisons are address comparisons,
and it works with `-fno-rtti`. `type_name<T>()` returns the readable name
(`"Point"`, `"std::vector<int>"`). Registry reads take no lock (readers load an
immutable snapshot); registration is a one-time cost.

## Dynamic access

`field` returns a type-erased `AnyRef` — a pointer, the field's `TypeId`,
`TypeKind`, name and a formatter. It never dangles beyond the object it points
into, so only lvalues are accepted: `field(make_point(), "x")` is a compile
error.

```cpp
Point p{7, 1.5f, 'a'};

auto x = fp::reflect::field(p, "x");     // Result<AnyRef>
*x.value().as<int>() = 9;                // typed read/write through the ref
x.value().type_name;                     // "int"

auto by_index = fp::reflect::field(p, std::size_t{1});   // y

auto missing = fp::reflect::field(p, "nope");            // error value
missing.error();                                         // "field: Point has no field 'nope'"
```

Typed helpers check the type and explain the mismatch:

```cpp
auto px = fp::reflect::get_field<int>(p, "x");   // Result<int*>
fp::reflect::get_field<float>(p, "x");           // error: "field 'x' is int, not float"

fp::reflect::set_field(p, "x", 21);              // Result<void>
fp::reflect::set_field(p, "x", std::string("no")); // error, type mismatch
```

To walk the fields dynamically — printing, serialising, diffing:

```cpp
fp::reflect::for_each_field_ref(p, [](const fp::reflect::FieldInfo &info,
                                      fp::reflect::AnyRef ref) {
  std::cout << info.name << " = " << fp::reflect::to_string(ref) << "\n";
});
```

`to_string(AnyRef)` formats `bool`, integers, floats, chars, strings, enums
(by enumerator name) and pointers, and falls back to the type name. A `const`
object yields `ConstAnyRef`, whose `as<T>()` returns a `const T*`.

## Printing, comparing and copying

A whole reflected object prints field by field, recursing into nested
reflected fields:

```cpp
Point p{7, 1.5f, 'a'};
fp::reflect::to_string(p);                 // "Point{x=7, y=1.5, tag='a'}"

struct Nested { Point p; std::string label; };
fp::reflect::to_string(Nested{p, "hi"});
// "Nested{p=Point{x=7, y=1.5, tag='a'}, label=hi}"
```

`equal` compares two reflected objects **field by field, matching by name**, so
different declaration orders compare equal:

```cpp
struct A { int id; float ratio; };
struct B { float ratio; int id; };       // same fields, other order
fp::reflect::equal(A{1, 2.0f}, B{2.0f, 1});   // true
```

Every field must exist in both with the same type; nested reflected fields
compare recursively, and trivially copyable values of other kinds compare their
bytes.

`copy_fields` is the struct-mapping workhorse: it copies every same-named,
type-compatible field and reports how many it moved.

```cpp
B dst{};
const auto n = fp::reflect::copy_fields(dst, A{1, 2.0f});   // 2
```

## Field offsets

Offsets are not available at compile time in the macro-free design, so they are
computed on demand. This needs a standard-layout type:

```cpp
fp::reflect::field_offset<0, Point>();   // 0
fp::reflect::field_offsets<Point>();     // std::array<std::size_t, 3>
```

## Enums

```cpp
enum class Colour { Red, Green, Blue };

fp::reflect::enum_name(Colour::Green);             // "Green"
fp::reflect::enum_name(static_cast<Colour>(7));    // "" (no enumerator)
fp::reflect::enum_from_name<Colour>("Green");      // optional<Colour>
fp::reflect::enum_values<Colour>();                // {Red, Green, Blue}
fp::reflect::is_valid_enum(Colour::Red);           // true
fp::reflect::enum_name_exact<Colour, Colour::Blue>(); // "Blue"
```

Enumerators are discovered by asking the compiler to spell out each value in a
scan range: `[-128, 127]` for signed underlying types, `[0, 255]` for unsigned
ones. Values outside the range are not listed by `enum_name`/`enum_values`, but
`enum_name_exact<E, V>()` works for any single constant. Duplicate values keep
the first (lowest) enumerator.

## Non-aggregates: `FieldAccess`

Classes with constructors or private members cannot be decomposed, so they opt
in by specialising `fp::reflect::FieldAccess`. The `members<...>` helper
generates `count` and a value-category-preserving `get<I>` from member
pointers, so only the metadata table has to be written out:

```cpp
struct Widget {
  explicit Widget(int v) : value(v) {}
  int value;
  float weight = 1.0f;
};

template <> struct fp::reflect::FieldAccess<Widget>
    : fp::reflect::members<&Widget::value, &Widget::weight> {
  static constexpr FieldInfo value[] = {
      {"value", type_id<int>(), type_name<int>(), sizeof(int)},
      {"weight", type_id<float>(), type_name<float>(), sizeof(float)}};
  static constexpr FieldInfo const *fields = value;
};
```

After that, `arity`, `field_at`, `describe`, `field`/`get_field`/`set_field`,
`to_string`, `equal`, `copy_fields` and `for_each_field_ref` all work on
`Widget` exactly as on an aggregate. (Names stay in the table because
`std::string_view` is not a structural type and so cannot be a template
argument; the pointer entries could not be avoided, but everything else is
generated.)

This is also the escape hatch when the automatic name recovery is unavailable
or wrong.

## Compile-time cost

Reflection's compile-time cost is dominated by **field-name extraction**, which
is the Boost.PFR technique: one compiler-printed pointer spelling per type,
parsed at compile time. On GCC 16, 40 structs × 30 fields, `-fsyntax-only`:

| what you use | time |
|---|---|
| include the header | 0.9 s |
| `arity<T>()` only | 1.0 s |
| `field_at` / `for_each_field` (no names) | 1.0 / 1.7 s |
| `describe_shape<T>()` (no names) | ~1.7 s |
| `fields<T>()` (the name table) | 3.8 s |
| `describe<T>()` / `type_info<T>()` | 3.8 / 3.6 s |

The names of all fields are extracted in **one** pretty-function instantiation
per type, which is several times cheaper than one per field (the pre-0.2
implementation): the same workload took 5.8 s before.

Practical guidance:

- **Structural work is nearly free.** `arity`, `field_at`, `for_each_field`,
  `type_id`, `type_name`, `describe_shape` never touch names.
- **Names are paid per type, per translation unit.** Any TU that calls
  `fields<T>()`, `describe<T>()`, `type_info<T>()`, `field(obj, name)`,
  `for_each_field_ref` or `to_string` instantiates the table. In a large
  project, keep the named path in a few TUs, or declare an explicit
  instantiation and use `extern template` to share it:
  ```cpp
  // registry.cpp
  template struct fp::reflect::detail::field_table<GameObject>;
  // registry.hpp, after the definition is visible
  extern template struct fp::reflect::detail::field_table<GameObject>;
  ```
- Prefer `describe_shape<T>()` when only layout matters.
- Everything below is `constexpr`-friendly, so unused pieces cost nothing.

## Runtime cost

Measured with `-O2` over 4096 objects (`Point`-sized structs; ns per call):

| path | time | notes |
|---|---|---|
| `field_at<I>` | 0.7 ns | same as a direct member access |
| `for_each_field` (typed) | 2.9 ns | per object |
| `field(obj, index)` | 0.7 ns | folded when the index is constant |
| `field(obj, "name")` | 5.5 ns | index-first: one `AnyRef`, no array |
| `get_field` / `set_field` | 7.8 / 8.2 ns | includes the type check |
| `for_each_field_ref` | 20 ns | per object (12 fields), erased refs |
| `to_string(AnyRef)` | 35 ns | `std::to_chars`, no allocation until the result |
| `find_type("name")` | 6 ns | lock-free snapshot read |
| `type_info<T>()` | 1.4 ns | cached |
| `describe<T>()` | 0.1 ns | constexpr, folded |
| `enum_name` | 0.1 ns | folded |

Rules of thumb: use the compile-time API in hot loops, `field_at<I>` when the
index is known, and the erased `AnyRef` path for tools (inspectors,
serialisers, debug UI) rather than per-frame game logic.

## Limits and notes

- **Aggregates only** (plus `FieldAccess` opt-ins). No unions, no virtual
  bases, no private members.
- **At most `max_fields` (32) fields.** Aggregates with more are treated as not
  reflectable (`arity` is 0) instead of silently reporting a truncated count.
- **Field names are compiler-recovered** on GCC and Clang. On other compilers
  (or if the trick breaks) `names_available` is `false` and names degrade to
  `field0`, `field1`, … — structure and typed access still work. C arrays and
  tuple-like types always use index names, since their elements have no member
  names.
- **Reflected types should be copy-constructible**: field counting initialises
  an aggregate from placeholder conversions.
- **Field offsets** need `std::is_standard_layout_v<T>` and are computed at
  runtime.
- **`field`/`get_field` take lvalues only.** The returned reference points into
  the object; a temporary would dangle, so it is rejected at compile time.
- **The registry is thread-safe** and reads are lock-free; `type_info<T>()`
  registers on first use.
- **`AnyRef` is a non-owning reference.** Keep the object alive while you use
  it.

## Cookbook

**Debug-print any reflected value.**

```cpp
fp::reflect::to_string(entity);                    // one call, fully recursive
for_each_field_ref(entity, [](auto const &info, auto ref) {
  std::cout << info.name << " = " << fp::reflect::to_string(ref) << '\n';
});
```

**Map between two similar types (DTO ↔ domain model).**

```cpp
const auto copied = fp::reflect::copy_fields(dto, entity);   // by field name
if (!fp::reflect::equal(dto, entity)) { /* report which fields differ */ }
```

**A property inspector (immediate-mode UI).**

```cpp
for_each_field_ref(selected, [&](const FieldInfo &info, AnyRef ref) {
  if (auto *v = ref.as<float>())
    *v = ui.drag_float(info.name, *v);      // edit in place
  else if (auto *i = ref.as<int>())
    *i = ui.drag_int(info.name, *i);
  else
    ui.label(info.name, fp::reflect::to_string(ref));
});
```

**Generic validation over any reflected type.**

```cpp
template <class T> bool has_finite_numbers(T const &value) {
  bool ok = true;
  for_each_field_ref(value, [&](const FieldInfo &, ConstAnyRef ref) {
    if (auto *d = ref.as<double>())
      ok = ok && std::isfinite(*d);
  });
  return ok;
}
```

**Adapt generic code to optional fields.**

```cpp
template <class T> auto describe_position(T const &obj) -> std::string {
  if constexpr (fp::reflect::has_field<T>("position")) {
    constexpr auto i = fp::reflect::field_index<T>("position");
    return fp::reflect::to_string(fp::reflect::field_at<i>(obj));
  } else {
    return "<none>";
  }
}
```
