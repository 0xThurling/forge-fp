# Strings — `string.hpp`

String utilities live in `namespace fp::str`. All functions take `std::string`
by value (so you can move in) and return a new `std::string` (or
`std::vector<std::string>` / `Result`).

```cpp
#include <fp/string.hpp>
using namespace fp::str;
```

**Why this module exists:** the stdlib's string ops (`substr`, `find`,
`getline`) are low-level primitives that force index bookkeeping and
`std::string::npos` checks. `fp::str` wraps the common cases as pure functions,
so string code reads as a pipeline instead of a cursor dance. The two deltas
from the stdlib are the *pure* signatures (input by value, output by value) and
`to_int`/`to_double` returning `Result` instead of throwing.

## Case

```cpp
to_lower("HELLO");   // "hello"
to_upper("hello");   // "HELLO"
capitalize("hi");    // "Hi"
title("a b c");      // "A B C"
```

## Trimming and padding

```cpp
trim("  hi  ");              // "hi"
trim_leading("  hi");        // "hi"
trim_trailing("hi  ");       // "hi"
trim_leading("..hi..", '.'); // "hi.."   (leading/trailing overloads take a char)
trim_trailing(trim_leading("..hi..", '.'), '.');  // "hi" (full custom-char trim)
pad_left("7", 3, '0');       // "007"
pad_right("7", 3, ' ');      // "7  "
```

## Splitting and joining

```cpp
split("a,b,c", ',');              // {"a","b","c"}
split("a--b", "--");              // {"a","b"}  (string delimiter)
lines("a\nb");                    // {"a","b"}
chunk("abcdef", 3);               // {"abc","def"}

join(std::vector<std::string>{"a","b","c"}, ", ");   // "a, b, c"
join(std::vector<int>{1,2,3}, "-");                  // "1-2-3"  (range overload)
```

`split`/`join` are inverses; both `split` overloads (char and string delimiter)
and both `join` overloads (vector and range) exist. `join` appends strings
directly and formats numbers with `std::to_chars` (a streamable-only type still
works through an `ostringstream` fallback).

Both are allocation-light: the char `split` uses one `find`/`substr` pass
(a `stringstream` + `getline` version was ~7x slower), `lines` is `split` on
`\n`, and `join` reserves the total size first.

When you don't need owned copies, `split_view` returns `vector<string_view>`
pointing into the original buffer — no allocation per piece:

```cpp
auto fields = split_view("a,b,c", ',');   // views into the literal
fields[1];                                 // "b"
```

Both split flavors drop a trailing empty field (`"a,b,"` → `{"a","b"}`) and
treat an empty delimiter as "no split".

## Prefixes, suffixes, replacement

```cpp
starts_with("hello", "he");     // true
ends_with("hello", "lo");       // true
strip_prefix("--hi", "--");     // "hi"
strip_suffix("hi--", "--");     // "hi"
replace_all("a-b-a", "a", "x"); // "x-b-x"
```

## Repetition and truncation

```cpp
repeat("ab", 3);            // "ababab"
truncate("hello world", 5); // "he..."   (custom tail: truncate(s, max, tail))
reverse("abc");             // "cba"
```

## Parsing into numbers

`to_int` / `to_double` return `Result` so failures compose instead of throwing:

```cpp
auto n = to_int("42");           // Result<int> ok(42)
auto x = to_int("  not ");       // Result<int> err("not a number")
auto d = to_double("3.14");      // Result<double> ok(3.14)

// compose with the ADT machinery:
to_int("42") >>= [](int i) { return fp::ok(i * 2); };   // Result<int> ok(84)
```

This is the bridge into [the ADTs](adts.md): a string that might not be a
number is a `Result`, so "parse then use" is `>>=` instead of try/catch.

## Precision formatting

`std::to_string` gives six decimals and cannot round-trip a `double`.
`fp::str::to_string` takes a precision:

```cpp
to_string(0.1 + 0.2, 17);   // "0.30000000000000004" — round-trips exactly
to_string(3.14159, 3);      // "3.14"
to_string(42);              // "42" (int overload)
```

The default `17` is `max_digits10` for `double`: the shortest decimal that
round-trips every `double`. Use it for storage and logs where exactness
matters, and a small precision for human-facing output.

## Splitting on any delimiter

`split` takes one delimiter; `split_any` takes a set of characters and treats
runs of them as one separator:

```cpp
split_any("a, b;;c\t d", ",;\t ");   // {"a", "b", "c", "d"}
split_any("  ,,  ", ", ");           // {} — empty fields are dropped
```

This is the CSV/whitespace workhorse: headers, config lines, and column
parsing.

## Parsing a list of numbers

`parse_numbers<T>` combines `split_any` with `to_int`/`to_double` and reports
the offending token:

```cpp
auto xs = parse_numbers<double>("1, 2.5  3");   // ok({1.0, 2.5, 3.0})
auto is = parse_numbers<int>("1 2 3");          // ok({1, 2, 3})

auto bad = parse_numbers<double>("1 x 3");
// err("not a number: 'x'")
```

It is the reader for files written with
[`fp::to_text`](serialization.md) and for numeric CSV columns, and it composes
with the rest of the library:

```cpp
auto total = fp::read_file("numbers.txt")
    >>= [](std::string const &text) { return parse_numbers<double>(text); }
    >>= [](std::vector<double> const &ns) { return fp::ok(fp::sum(ns)); };
```

## Which function when

| Need | Function |
|---|---|
| One delimiter, owned pieces | `split(s, delim)` |
| One delimiter, no copies | `split_view(s, delim)` |
| Any of several delimiters | `split_any(s, delims)` |
| Line structure | `lines(s)` |
| Fixed-width chunks | `chunk(s, n)` |
| Join pieces | `join(parts, sep)` |
| Parse one number | `to_int` / `to_double` |
| Parse a whole list | `parse_numbers<T>` |
| Format a number exactly | `to_string(value, precision)` |

## Gotchas

- **`split_view` borrows.** The views point into the original string; if that
  string dies, the views dangle. Materialize with `split` when the source is a
  temporary.
- **Trailing empties are dropped.** `split("a,b,", ',')` is `{"a","b"}`, not
  `{"a","b",""}`. `split_any` drops interior empties too.
- **`trim` only trims spaces by default.** For other characters use the
  `char` overloads (`trim_leading(s, c)`), or compose them.
- **`to_int` accepts a leading `+`/`-` and surrounding whitespace** but rejects
  anything after the digits (`"42x"` fails). That strictness is deliberate, and
  an invalid field costs ~11ns rather than the ~1.1us an exception used to cost.
- **`to_string` defaults to 17 digits.** That is right for storage and noisy
  for display; pass a smaller precision for logs.
- **Locale.** `to_string` and `to_double` are locale-independent (`to_chars`/
  `from_chars`), so "C" formatting is what you get in every locale. That is what
  data files want; for user-facing display, format through your own stream.
- **Strings are byte sequences.** `to_lower`/`to_upper` handle ASCII; Unicode
  case mapping needs a real library.
