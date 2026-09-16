# Strings — `string.hpp`

String utilities live in `namespace fp::str`. All functions take `std::string`
by value (so you can move in) and return a new `std::string` (or
`std::vector<std::string>` / `Result`).

```cpp
#include <fp/string.hpp>
using namespace fp::str;
```

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
