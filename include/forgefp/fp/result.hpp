#pragma once
#include "either.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace fp {
template <class T> using Result = Either<std::string, T>;

template <class T> Result<T> ok() { return Result<T>::ok(); }
template <class T> Result<T> ok(T t) { return Result<T>::ok(std::move(t)); }

template <class T> Result<T> err(std::string msg) {
  return Result<T>::err(std::move(msg));
}

template <class T>
Result<std::vector<T>> sequence(std::vector<Result<T>> const &rs) {
  std::vector<T> out;
  out.reserve(rs.size());
  for (auto const &r : rs) {
    if (!r.is_ok())
      return err<std::vector<T>>(r.error());
    out.push_back(r.value());
  }
  return ok(std::move(out));
}

template <class T>
Result<T> from_optional(std::optional<T> const &o, std::string msg) {
  return o ? ok(*o) : err<T>(std::move(msg));
}

template <class T>
Result<std::optional<T>> transpose(std::optional<Result<T>> const &o) {
  if (!o)
    return ok(std::optional<T>{std::nullopt});
  if (!o->is_ok())
    return err<std::optional<T>>(o->error());
  return ok(std::optional<T>(o->value()));
}

template <class T>
std::optional<Result<T>> transpose(Result<std::optional<T>> const &r) {
  if (!r.is_ok())
    return Result<T>::err(r.error());
  return r.value() ? std::optional<Result<T>>(ok<T>(*r.value())) : std::nullopt;
}

template <class F> Result<std::invoke_result_t<F>> try_(F f) {
  using R = std::invoke_result_t<F>;
  try {
    return ok<R>(f());
  } catch (std::exception const &e) {
    return err<R>(e.what());
  } catch (...) {
    return err<R>("unknown exception");
  }
}

template <class T, class F>
Result<std::vector<std::invoke_result_t<F, T>>>
traverse(std::vector<T> const &v, F f) {
  using R = std::invoke_result_t<F, T>;
  std::vector<R> out;
  out.reserve(v.size());
  for (auto const &x : v) {
    auto r = f(x);
    if (!r.is_ok())
      return err<std::vector<R>>(r.error());
    out.push_back(r.value());
  }
  return ok(std::move(out));
}

template <class A, class B>
Result<std::pair<A, B>> combine2(Result<A> const &a, Result<B> const &b) {
  if (!a.is_ok())
    return err<std::pair<A, B>>(a.error());
  if (!b.is_ok())
    return err<std::pair<A, B>>(b.error());
  return ok<std::pair<A, B>>({a.value(), b.value()});
}

template <class T>
Result<T> context(Result<T> const &r, std::string const &prefix) {
  return map_error(r, [&](std::string const &e) { return prefix + e; });
}

template <class T>
Result<std::vector<T>> collect_all(std::vector<Result<T>> const &rs) {
  std::vector<T> values;
  std::vector<std::string> errors;
  for (auto const &r : rs) {
    if (r.is_ok())
      values.push_back(r.value());
    else
      errors.push_back(r.error());
  }
  if (errors.empty())
    return ok(std::move(values));
  std::string joined;
  for (size_t i = 0; i < errors.size(); ++i)
    joined += (i ? "; " : "") + errors[i];
  return err<std::vector<T>>(joined);
}
} // namespace fp
