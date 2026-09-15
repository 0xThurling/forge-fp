#pragma once
#include "either.hpp"
#include "fp/result.hpp"
#include <string>
#include <type_traits>
#include <vector>

namespace fp {
template <class T> using Validation = Either<std::vector<std::string>, T>;

template <class T> Validation<T> valid(T t) {
  return Validation<T>::ok(std::move(t));
}

template <class T> Validation<T> invalid(std::string msg) {
  return Validation<T>::err(std::vector<std::string>{std::move(msg)});
}

template <class T> Validation<T> invalid(std::vector<std::string> msgs) {
  return Validation<T>::err(std::move(msgs));
}

template <class T>
Validation<std::vector<T>> validate_all(std::vector<Validation<T>> const &vs) {
  std::vector<std::string> errs;
  std::vector<T> out;

  for (auto const &v : vs) {
    if (v.is_ok())
      out.push_back(v.value());
    else
      errs.insert(errs.end(), v.error().begin(), v.error().end());
  }

  return errs.empty() ? Validation<std::vector<T>>::ok(std::move(out))
                      : Validation<std::vector<T>>::err(std::move(errs));
}

template <class A, class B, class F>
auto combine2(Validation<A> const &a, Validation<B> const &b, F make)
    -> Validation<std::invoke_result_t<F, A, B>> {
  std::vector<std::string> errs;
  if (!a.is_ok())
    errs.insert(errs.end(), a.error().begin(), a.error().end());
  if (!b.is_ok())
    errs.insert(errs.end(), b.error().begin(), b.error().end());
  if (!errs.empty())
    return Validation<std::invoke_result_t<F, A, B>>::err(errs);
  return Validation<std::invoke_result_t<F, A, B>>::ok(
      make(a.value(), b.value()));
}

template <class T> Validation<T> ensure(bool ok, std::string msg, T value) {
  return ok ? valid<T>(std::move(value)) : invalid<T>(std::move(msg));
}

template <class T, class Pred>
Validation<T> check(Pred pred, std::string msg, T value) {
  return pred(value) ? valid<T>(std::move(value)) : invalid<T>(std::move(msg));
}

template <class T, class F>
Validation<std::vector<std::invoke_result_t<F, T>>>
traverse(std::vector<T> const &v, F f) {
  using R = std::invoke_result_t<F, T>;
  std::vector<R> values;
  std::vector<std::string> errors;
  for (auto const &x : v) {
    auto r = f(x);
    if (r.is_ok()) {
      values.push_back(r.value());
    } else {
      auto const &msgs = r.error();
      errors.insert(errors.end(), msgs.begin(), msgs.end());
    }
  }
  if (!errors.empty())
    return invalid<std::vector<R>>(std::move(errors));
  return valid(std::move(values));
}

template <class F, class... Vs> auto combine(F on_success, Vs const &...vs) {
  using R = std::invoke_result_t<F, decltype(vs.value())...>;
  std::vector<std::string> errors;
  auto collect = [&errors](auto const &v) {
    if (!v.is_ok()) {
      auto const &msgs = v.error();
      errors.insert(errors.end(), msgs.begin(), msgs.end());
    }
  };
  (collect(vs), ...);
  if (!errors.empty())
    return invalid<R>(std::move(errors));

  return valid<R>(on_success(vs.value()...));
}

template <class T>
Validation<T> merge(Validation<T> const &a, Validation<T> const &b) {
  if (a.is_ok() && b.is_ok())
    return valid(a.value());
  std::vector<std::string> errors;
  for (auto const *v : {&a, &b})
    if (!v->is_ok()) {
      auto const &msgs = v->errors();
      errors.insert(errors.end(), msgs.begin(), msgs.end());
    }
  return invalid<T>(std::move(errors));
}

template <class T> Result<T> to_result(Validation<T> const &v) {
  if (v.is_ok())
    return ok(v.value());
  auto const &msgs = v.error();
  return err<T>(msgs.empty() ? "validation failed" : msgs.front());
}

template <class T, class Pred>
Validation<std::vector<T>> validate_none(std::vector<T> const &vs, Pred pred,
                                         std::string msg) {
  std::vector<std::string> errors;
  for (auto const &x : vs)
    if (pred(x))
      errors.push_back(msg);
  if (!errors.empty())
    return invalid<std::vector<T>>(std::move(errors));
  return valid(vs);
}

template <class T, class Pred>
Validation<std::vector<T>> validate_any(std::vector<T> const &vs, Pred pred,
                                        std::string msg) {
  for (auto const &x : vs)
    if (pred(x))
      return valid(vs);
  return invalid<std::vector<T>>(std::move(msg));
}
} // namespace fp
