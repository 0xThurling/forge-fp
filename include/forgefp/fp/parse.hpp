#pragma once
#include "result.hpp"
#include <cctype>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace fp {

// A parse error: a message plus the byte offset where it occurred. `run`
// converts the offset to a line/column for the user.
struct ParseError {
  std::string message;
  std::size_t offset = 0;
};

template <class T>
using PResult = Either<ParseError, std::pair<T, std::string_view>>;

template <class T> PResult<T> p_ok(T v, std::string_view rest) {
  return PResult<T>::ok(std::pair<T, std::string_view>{std::move(v), rest});
}

template <class T> PResult<T> p_err(std::string msg, std::size_t off) {
  return PResult<T>::err(ParseError{std::move(msg), off});
}

// A parser: `(remaining input, absolute offset) -> PResult<value>`.
// Tracking the offset (rather than only the remaining suffix) is what lets
// errors carry real positions. Callable like a function; constructs implicitly
// from a matching lambda; carries operators.
template <class T> struct Parser {
  using value_type = T;
  using result_type = PResult<T>;
  using fn_type = std::function<result_type(std::string_view, std::size_t)>;

  fn_type fn;

  Parser() = default;

  template <class F>
    requires(!std::is_same_v<std::remove_cvref_t<F>, Parser>) &&
            std::is_invocable_r_v<result_type, F &, std::string_view, std::size_t>
  Parser(F &&f) : fn(std::forward<F>(f)) {}

  result_type operator()(std::string_view s, std::size_t off = 0) const {
    return fn(s, off);
  }

  // Annotate failures with context (prefixes the message).
  Parser label(std::string msg) const {
    auto p = *this; // `this` is const: one copy, then moved into the lambda
    return Parser([p = std::move(p), msg = std::move(msg)](std::string_view s,
                                            std::size_t off) -> result_type {
      auto r = p(s, off);
      if (r.is_ok())
        return r;
      return result_type::err(
          ParseError{msg + ": " + r.error().message, r.error().offset});
    });
  }
};

// --- primitives -------------------------------------------------------------

inline Parser<char> char_(char c) {
  return [c](std::string_view s, std::size_t off) -> PResult<char> {
    if (s.empty() || s.front() != c)
      return p_err<char>("expected '" + std::string(1, c) + "'", off);
    return p_ok(c, s.substr(1));
  };
}

inline Parser<std::string> string_(std::string_view t) {
  return [t = std::string(t)](std::string_view s,
                              std::size_t off) -> PResult<std::string> {
    if (s.size() < t.size() || s.substr(0, t.size()) != t)
      return p_err<std::string>("expected \"" + t + "\"", off);
    return p_ok(t, s.substr(t.size()));
  };
}

template <class Pred> Parser<char> satisfy(Pred pred) {
  return [pred = std::move(pred)](std::string_view s,
                                  std::size_t off) -> PResult<char> {
    if (s.empty() || !pred(s.front()))
      return p_err<char>("no match", off);
    return p_ok(s.front(), s.substr(1));
  };
}

inline Parser<char> digit = satisfy(
    [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
inline Parser<char> letter = satisfy(
    [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; });
inline Parser<char> alnum = satisfy(
    [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
inline Parser<char> space = satisfy(
    [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });

template <class... Cs> Parser<char> one_of(Cs... cs) {
  return satisfy([cs...](char c) { return ((c == cs) || ...); });
}
template <class... Cs> Parser<char> none_of(Cs... cs) {
  return satisfy([cs...](char c) { return ((c != cs) && ...); });
}

// Greedily consume characters matching `pred` and yield the slice (a view into
// the input). This is the allocation-free way to scan a token: `many(digit)`
// builds a std::vector<char> per token, scan_while1 returns a string_view.
// Parsers receive the *remaining* input as `s` and the absolute position as
// `off` (kept only for error messages).
template <class Pred>
Parser<std::string_view> scan_while(Pred pred) {
  return [pred = std::move(pred)](
             std::string_view s,
             [[maybe_unused]] std::size_t off) -> PResult<std::string_view> {
    std::size_t n = 0;
    while (n < s.size() && pred(s[n]))
      ++n;
    return p_ok(s.substr(0, n), s.substr(n));
  };
}

// Like scan_while, but requires at least one character.
template <class Pred>
Parser<std::string_view> scan_while1(Pred pred) {
  return [pred = std::move(pred)](std::string_view s,
                                  std::size_t off) -> PResult<std::string_view> {
    std::size_t n = 0;
    while (n < s.size() && pred(s[n]))
      ++n;
    if (n == 0)
      return p_err<std::string_view>("expected at least one character", off);
    return p_ok(s.substr(0, n), s.substr(n));
  };
}

// Succeeds only at the end of input, yielding nothing.
inline Parser<std::monostate> eof = [](std::string_view s,
                                       std::size_t off) -> PResult<std::monostate> {
  if (!s.empty())
    return p_err<std::monostate>("expected end of input", off);
  return p_ok(std::monostate{}, s);
};

// Every parser is called as f(remaining_input, absolute_offset) and returns the
// value plus the new remaining input. `off` is only used to position errors, so
// a primitive scans the *front* of `s`, not s[off].
//
// Cost note: Parser<T> type-erases its callable in a std::function, so a
// pipeline of N combinators pays N indirect calls per position. Prefer
// scan_while/scan_while1 over many/many1 when scanning characters: the former
// yields a string_view slice, the latter a fresh std::vector<char> per token
// (measured on bench/parse_bench.cpp: ~4x faster for a list of integers).
//
// --- repetition and choice --------------------------------------------------

template <class T> Parser<std::vector<T>> many(Parser<T> p) {
  return [p = std::move(p)](std::string_view s,
                            std::size_t off) -> PResult<std::vector<T>> {
    std::vector<T> out;
    std::size_t cur = off;
    for (;;) {
      auto r = p(s, cur);
      if (!r.is_ok())
        break;
      std::size_t consumed = s.size() - r.value().second.size();
      if (consumed == 0)
        break; // avoid spinning on a zero-width parser
      out.push_back(std::move(r.value().first));
      s = r.value().second;
      cur += consumed;
    }
    return p_ok(std::move(out), s);
  };
}

template <class T> Parser<std::vector<T>> some(Parser<T> p) {
  return [p = std::move(p)](std::string_view s,
                            std::size_t off) -> PResult<std::vector<T>> {
    std::vector<T> out;
    std::size_t cur = off;
    for (;;) {
      auto r = p(s, cur);
      if (!r.is_ok())
        break;
      std::size_t consumed = s.size() - r.value().second.size();
      if (consumed == 0)
        break;
      out.push_back(std::move(r.value().first));
      s = r.value().second;
      cur += consumed;
    }
    if (out.empty())
      return p_err<std::vector<T>>("expected at least one item", off);
    return p_ok(std::move(out), s);
  };
}

template <class T> Parser<std::vector<T>> many1(Parser<T> p) {
  return some(std::move(p));
}

template <class T, class D>
Parser<std::vector<T>> sep_by(Parser<T> p, Parser<D> sep) {
  return [p = std::move(p),
          sep = std::move(sep)](std::string_view s,
                                std::size_t off) -> PResult<std::vector<T>> {
    std::vector<T> out;
    std::size_t cur = off;
    auto first = p(s, cur);
    if (!first.is_ok())
      return p_ok(std::move(out), s);
    out.push_back(std::move(first.value().first));
    cur += s.size() - first.value().second.size();
    s = first.value().second;

    for (;;) {
      auto sp = sep(s, cur);
      if (!sp.is_ok())
        return p_ok(std::move(out), s);
      std::size_t after_sep = cur + (s.size() - sp.value().second.size());
      auto item = p(sp.value().second, after_sep);
      if (!item.is_ok())
        return p_err<std::vector<T>>("expected item after separator", after_sep);
      out.push_back(std::move(item.value().first));
      s = item.value().second;
      cur = after_sep + (sp.value().second.size() - s.size());
    }
  };
}

template <class T>
Parser<std::optional<T>> optional(Parser<T> p) {
  return [p = std::move(p)](std::string_view s,
                            std::size_t off) -> PResult<std::optional<T>> {
    auto r = p(s, off);
    if (r.is_ok())
      return p_ok(std::optional<T>(std::move(r.value().first)),
                  r.value().second);
    return p_ok(std::optional<T>(std::nullopt), s);
  };
}

// Try `a`; on failure, try `b` from the same position.
template <class T> Parser<T> alt(Parser<T> a, Parser<T> b) {
  return [a = std::move(a), b = std::move(b)](std::string_view s,
                                              std::size_t off) -> PResult<T> {
    auto r = a(s, off);
    return r.is_ok() ? r : b(s, off);
  };
}

// --- sequencing -------------------------------------------------------------

template <class A, class F>
auto map(Parser<A> p, F f) -> Parser<std::invoke_result_t<F, A>> {
  using B = std::invoke_result_t<F, A>;
  return [p = std::move(p),
          f = std::move(f)](std::string_view s, std::size_t off) -> PResult<B> {
    auto r = p(s, off);
    if (!r.is_ok())
      return p_err<B>(r.error().message, r.error().offset);
    return p_ok(f(std::move(r.value().first)), r.value().second);
  };
}

template <class A, class F>
auto and_then(Parser<A> p, F f) -> Parser<typename std::invoke_result_t<F, A>::value_type> {
  using B = typename std::invoke_result_t<F, A>::value_type;
  return [p = std::move(p),
          f = std::move(f)](std::string_view s, std::size_t off) -> PResult<B> {
    auto r = p(s, off);
    if (!r.is_ok())
      return p_err<B>(r.error().message, r.error().offset);
    std::size_t next = off + (s.size() - r.value().second.size());
    return f(std::move(r.value().first))(r.value().second, next);
  };
}

template <class A, class B>
Parser<std::pair<A, B>> seq(Parser<A> a, Parser<B> b) {
  return and_then(a, [b = std::move(b)](A av) {
    return map(b, [av = std::move(av)](B bv) {
      return std::make_pair(std::move(av), std::move(bv));
    });
  });
}

// parse `a`, then `b`, keep b's value
template <class A, class B> Parser<B> preceded(Parser<A> a, Parser<B> b) {
  return and_then(a, [b = std::move(b)](A) { return b; });
}

// parse `a`, then `b`, keep a's value
template <class A, class B> Parser<A> terminated(Parser<A> a, Parser<B> b) {
  return and_then(a, [b = std::move(b)](A av) {
    return map(b, [av = std::move(av)](B) { return av; });
  });
}

// parse open, p, close — keep p
template <class O, class C, class T>
Parser<T> between(Parser<O> open, Parser<C> close, Parser<T> p) {
  return terminated(preceded(std::move(open), std::move(p)), std::move(close));
}

// --- whitespace-aware lexing ------------------------------------------------

inline Parser<std::monostate> whitespace() {
  static Parser<std::monostate> ws =
      map(many(space), [](auto) { return std::monostate{}; });
  return ws;
}
template <class T> Parser<T> lexeme(Parser<T> p) {
  return terminated(std::move(p), whitespace());
}
inline Parser<char> symbol(char c) { return lexeme(char_(c)); }
inline Parser<std::string> keyword(std::string_view s) {
  return lexeme(string_(s));
}

// --- lookahead --------------------------------------------------------------

// Run `p` but do not consume; yield its value.
template <class T> Parser<T> peek(Parser<T> p) {
  return [p = std::move(p)](std::string_view s,
                            std::size_t off) -> PResult<T> {
    auto r = p(s, off);
    if (!r.is_ok())
      return r;
    return p_ok(std::move(r.value().first), s);
  };
}

// Succeed (with no value) only if `p` fails; never consumes (negative
// lookahead). Named `not_followed` to avoid `ops.hpp`'s `not_` logic operator.
template <class T> Parser<std::monostate> not_followed(Parser<T> p) {
  return [p = std::move(p)](std::string_view s,
                            std::size_t off) -> PResult<std::monostate> {
    auto r = p(s, off);
    if (r.is_ok())
      return p_err<std::monostate>("unexpected input", off);
    return p_ok(std::monostate{}, s);
  };
}

// --- variadic choice --------------------------------------------------------

template <class T> Parser<T> choice(Parser<T> a) { return a; }
template <class T, class... Rest>
Parser<T> choice(Parser<T> a, Parser<T> b, Rest... rest) {
  return alt(std::move(a), choice(std::move(b), std::move(rest)...));
}

// --- annotation -------------------------------------------------------------

template <class T> Parser<T> label(Parser<T> p, std::string msg) {
  return p.label(std::move(msg));
}
template <class T> Parser<T> context(Parser<T> p, std::string msg) {
  return p.label(std::move(msg));
}

// --- left-associative operator chains ---------------------------------------

// One or more `p` separated by `op`, folding left: `op` yields a binary
// callable invocable as `T(T, T)`.
template <class T, class F>
Parser<T> chainl1(Parser<T> p, Parser<F> op) {
  return [p = std::move(p), op = std::move(op)](
             std::string_view s, std::size_t off) -> PResult<T> {
    auto first = p(s, off);
    if (!first.is_ok())
      return first;
    T acc = std::move(first.value().first);
    std::size_t cur = off + (s.size() - first.value().second.size());
    s = first.value().second;

    for (;;) {
      auto o = op(s, cur);
      if (!o.is_ok())
        break;
      std::size_t rhs_off = cur + (s.size() - o.value().second.size());
      auto rhs = p(o.value().second, rhs_off);
      if (!rhs.is_ok())
        return rhs;
      acc = o.value().first(std::move(acc), std::move(rhs.value().first));
      s = rhs.value().second;
      cur = rhs_off + (o.value().second.size() - s.size());
    }
    return p_ok(std::move(acc), s);
  };
}

// --- recursion and constants ------------------------------------------------

template <class T> Parser<T> ref(Parser<T> &p) {
  return [&p](std::string_view s, std::size_t off) { return p(s, off); };
}

// always succeeds with `value`, consuming nothing (for `>>=`)
template <class T> Parser<T> succeed(T value) {
  return [value](std::string_view s, std::size_t) -> PResult<T> {
    return p_ok(value, s);
  };
}

// --- operators --------------------------------------------------------------
// a >> b : parse a then b, keep b
// a << b : parse a then b, keep a
// a | b  : try a, else b
// a >>= f: parse a, then run the parser `f(value)`
// *p     : zero or more
// p % sep: sep_by(p, sep)

template <class A, class B>
Parser<B> operator>>(Parser<A> a, Parser<B> b) {
  return preceded(std::move(a), std::move(b));
}
template <class A, class B>
Parser<A> operator<<(Parser<A> a, Parser<B> b) {
  return terminated(std::move(a), std::move(b));
}
template <class T> Parser<T> operator|(Parser<T> a, Parser<T> b) {
  return alt(std::move(a), std::move(b));
}
template <class A, class F>
auto operator>>=(Parser<A> a, F f) -> Parser<typename std::invoke_result_t<F, A>::value_type> {
  return and_then(std::move(a), std::move(f));
}
template <class T> Parser<std::vector<T>> operator*(Parser<T> p) {
  return many(std::move(p));
}
template <class T, class D>
Parser<std::vector<T>> operator%(Parser<T> p, Parser<D> sep) {
  return sep_by(std::move(p), std::move(sep));
}

// --- running ----------------------------------------------------------------

inline std::pair<std::size_t, std::size_t>
line_col(std::string_view input, std::size_t offset) {
  std::size_t line = 1, col = 1;
  for (std::size_t i = 0; i < offset && i < input.size(); ++i) {
    if (input[i] == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
  }
  return {line, col};
}

template <class T> Result<T> run(Parser<T> p, std::string_view input) {
  auto r = p(input, 0);
  if (!r.is_ok()) {
    auto [line, col] = line_col(input, r.error().offset);
    return err<T>("line " + std::to_string(line) + ", col " +
                  std::to_string(col) + ": " + r.error().message);
  }
  return ok(std::move(r.value().first));
}

} // namespace fp
