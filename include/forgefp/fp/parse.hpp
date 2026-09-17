#pragma once
#include "forgefp/fp/result.hpp"
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fp {

template <class T>
using Parser =
    std::function<Result<std::pair<T, std::string_view>>(std::string_view)>;

inline Parser<char> char_(char c) {
  return [c](std::string_view s) -> Result<std::pair<char, std::string_view>> {
    if (s.empty() || s.front() != c)
      return err<std::pair<char, std::string_view>>("expected '" +
                                                    std::string(1, c) + "'");
    return ok(std::pair<char, std::string_view>{c, s.substr(1)});
  };
}

inline Parser<std::string> string_(std::string_view t) {
  return [t = std::string(t)](std::string_view s)
      -> Result<std::pair<std::string, std::string_view>> {
    if (s.size() < t.size() || s.substr(0, t.size()) != t)
      return err<std::pair<std::string, std::string_view>>("expected \"" + t +
                                                           "\"");
    return ok(
        std::pair<std::string, std::string_view>{t, s.substr(t.size())});
  };
}

template <class T>
Parser<std::vector<T>> many(Parser<T> p) {
  return [p = std::move(p)](std::string_view s)
      -> Result<std::pair<std::vector<T>, std::string_view>> {
    std::vector<T> out;
    for (;;) {
      auto r = p(s);
      if (!r.is_ok())
        break;
      out.push_back(r.value().first);
      s = r.value().second;
    }
    return ok(std::pair<std::vector<T>, std::string_view>{std::move(out), s});
  };
}

template <class T>
Parser<std::vector<T>> some(Parser<T> p) {
  return [p = std::move(p)](std::string_view s)
      -> Result<std::pair<std::vector<T>, std::string_view>> {
    std::vector<T> out;
    for (;;) {
      auto r = p(s);
      if (!r.is_ok())
        break;
      out.push_back(r.value().first);
      s = r.value().second;
    }
    if (out.empty())
      return err<std::pair<std::vector<T>, std::string_view>>(
          "expected at least one item");
    return ok(std::pair<std::vector<T>, std::string_view>{std::move(out), s});
  };
}

template <class T, class D>
Parser<std::vector<T>> sep_by(Parser<T> p, Parser<D> sep) {
  return [p = std::move(p), sep = std::move(sep)](std::string_view s)
      -> Result<std::pair<std::vector<T>, std::string_view>> {
    std::vector<T> out;
    auto first = p(s);
    if (!first.is_ok())
      return ok(std::pair<std::vector<T>, std::string_view>{std::move(out), s});
    out.push_back(first.value().first);
    s = first.value().second;
    for (;;) {
      auto sp = sep(s);
      if (!sp.is_ok())
        return ok(
            std::pair<std::vector<T>, std::string_view>{std::move(out), s});
      auto item = p(sp.value().second);
      if (!item.is_ok())
        return err<std::pair<std::vector<T>, std::string_view>>(
            "expected item after separator");
      out.push_back(item.value().first);
      s = item.value().second;
    }
  };
}

template <class T>
Parser<std::optional<T>> optional(Parser<T> p) {
  return [p = std::move(p)](std::string_view s)
      -> Result<std::pair<std::optional<T>, std::string_view>> {
    auto r = p(s);
    if (r.is_ok())
      return ok(std::pair<std::optional<T>, std::string_view>{r.value().first,
                                                              r.value().second});
    return ok(
        std::pair<std::optional<T>, std::string_view>{std::nullopt, s});
  };
}

template <class A, class F>
auto map(Parser<A> p, F f) -> Parser<std::invoke_result_t<F, A>> {
  using B = std::invoke_result_t<F, A>;
  return [p = std::move(p), f = std::move(f)](std::string_view s)
      -> Result<std::pair<B, std::string_view>> {
    auto r = p(s);
    if (!r.is_ok())
      return err<std::pair<B, std::string_view>>(r.error());
    return ok(
        std::pair<B, std::string_view>{f(r.value().first), r.value().second});
  };
}

template <class A, class F>
auto and_then(Parser<A> p, F f)
    -> Parser<
        typename std::invoke_result_t<F, A>::result_type::value_type::first_type> {
  using ResultB = typename std::invoke_result_t<F, A>::result_type;
  using PairB = typename ResultB::value_type;
  using B = typename PairB::first_type;
  return Parser<B>([p = std::move(p), f = std::move(f)](std::string_view s)
                       -> Result<std::pair<B, std::string_view>> {
    auto r = p(s);
    if (!r.is_ok())
      return err<std::pair<B, std::string_view>>(r.error());
    return f(r.value().first)(r.value().second);
  });
}

template <class A>
Parser<A> alt(Parser<A> a, Parser<A> b) {
  return [a = std::move(a), b = std::move(b)](std::string_view s) {
    auto r = a(s);
    return r.is_ok() ? r : b(s);
  };
}

template <class T> Result<T> run(Parser<T> p, std::string_view s) {
  auto r = p(s);
  if (!r.is_ok())
    return err<T>(r.error());
  return ok(r.value().first);
}

} // namespace fp
