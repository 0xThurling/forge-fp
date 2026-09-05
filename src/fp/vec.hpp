#pragma once
#include <algorithm>
#include <cstddef>
#include <numeric>
#include <optional>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace fp {
template <class T, class F> auto map(std::vector<T> const &v, F f) {
  std::vector<std::invoke_result_t<F, T>> out;
  out.reserve(v.size());
  for (auto const &x : v)
    out.push_back(f(x));
  return out;
}

template <class T, class F>
std::vector<T> filter(std::vector<T> const &v, F pred) {
  std::vector<T> out;
  for (auto const &x : v)
    if (pred(x))
      out.push_back(x);
  return out;
}

template <class A, class B>
std::vector<std::pair<A, B>> zip(std::vector<A> const &a,
                                 std::vector<B> const &b) {
  std::vector<std::pair<A, B>> out;
  auto n = std::min(a.size(), b.size());
  out.reserve(n);
  for (size_t i = 0; i < n; ++i)
    out.emplace_back(a[i], b[i]);
  return out;
}

template <class T> std::optional<T> head(std::vector<T> const &v) {
  if (v.empty())
    return std::nullopt;
  return v.front();
}

template <class T, class F> auto partition(std::vector<T> const &v, F f) {
  std::vector<T> yes, no;
  for (auto const &x : v)
    (f(x) ? yes : no).push_back(x);
  return std::pair{yes, no};
}

template <class T, class F> auto group_by(std::vector<T> const &v, F key_fn) {
  using K = std::invoke_result_t<F, T>;
  std::unordered_map<K, std::vector<T>> out;
  for (auto const &x : v)
    out[key_fn(x)].push_back(x);
  return out;
}

template <class T>
std::vector<std::vector<T>> chunk(std::vector<T> const &v, size_t size) {
  std::vector<std::vector<T>> out;
  if (size == 0)
    return out;
  for (size_t i = 0; i < v.size(); i += size) {
    out.emplace_back(v.begin() + i, v.begin() + std::min(v.size(), i + size));
  }
  return out;
}

template <class A, class B, class F>
auto zip_with(std::vector<A> const &a, std::vector<B> const &b, F f) {
  std::vector<std::invoke_result_t<F, A, B>> out;
  auto n = std::min(a.size(), b.size());
  out.reserve(n);
  for (size_t i = 0; i < n; ++i)
    out.push_back(f(a[i], b[i]));
  return out;
}

template <class T> std::vector<T> take(std::vector<T> const &v, size_t n) {
  auto end = v.begin() + std::min(n, v.size());
  return std::vector<T>(v.begin(), end);
}

template <class T> std::vector<T> drop(std::vector<T> const &v, size_t n) {
  auto start = v.begin() + std::min(n, v.size());
  return std::vector<T>(start, v.end());
}

template <class T, class F>
std::vector<T> take_while(std::vector<T> const &v, F pred) {
  std::vector<T> out;
  for (auto const &x : v) {
    if (!pred(x))
      break;
    out.push_back(x);
  }
  return out;
}

template <class T, class F>
std::vector<T> drop_while(std::vector<T> const &v, F pred) {
  auto it = std::find_if_not(v.begin(), v.end(), pred);
  return std::vector<T>(it, v.end());
}

template <class T> std::vector<T> tail(std::vector<T> const &v) {
  return v.empty() ? std::vector<T>{} : std::vector<T>(v.begin() + 1, v.end());
}

template <class T> std::optional<T> last(std::vector<T> const &v) {
  return v.empty() ? std::nullopt : std::optional<T>(v.back());
}

template <class T> std::vector<T> init(std::vector<T> const &v) {
  return v.empty() ? std::vector<T>{} : std::vector<T>(v.begin(), v.end() - 1);
}

template <class T> std::vector<T> reverse(std::vector<T> const &v) {
  auto out = v;
  std::reverse(out.begin(), out.end());
  return out;
}

template <class T> std::vector<T> sort(std::vector<T> const &v) {
  auto out = v;
  std::ranges::sort(out);
  return out;
}

template <class T, class F>
std::vector<T> sort_by(std::vector<T> const &v, F key_fn) {
  auto out = v;
  std::ranges::sort(
      out, [&](T const &a, T const &b) { return key_fn(a) < key_fn(b); });
}

template <class T, class F> bool all(std::vector<T> const &v, F pred) {
  return std::ranges::all_of(v, pred);
}

template <class T, class F> bool any(std::vector<T> const &v, F pred) {
  return std::ranges::any_of(v, pred);
}

template <class T, class F> bool none(std::vector<T> const &v, F pred) {
  return std::ranges::none_of(v, pred);
}

template <class T, class F>
std::optional<size_t> find(std::vector<T> const &v, F pred) {
  auto it = std::ranges::find_if(v, pred);
  return it == v.end() ? std::nullopt
                       : std::optional<size_t>(std::distance(v.begin(), it));
}

template <class T> bool contains(std::vector<T> const &v, T const &x) {
  return std::ranges::find(v, x) != v.end();
}

template <class T, class F> size_t count_if(std::vector<T> const &v, F pred) {
  return std::ranges::count_if(v, pred);
}

template <class T>
std::vector<T> concat(std::vector<std::vector<T>> const &vs) {
  std::vector<T> out;
  for (auto const &v : vs)
    out.insert(out.end(), v.begin(), v.end());
  return out;
}

template <class T, class F> auto flat_map(std::vector<T> const &v, F f) {
  using R = std::invoke_result_t<F, T>;
  std::vector<typename R::value_type> out;
  for (auto const &x : v) {
    auto part = f(x);
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

template <class T>
std::vector<std::pair<size_t, T>> enumerate(std::vector<T> const &v) {
  std::vector<std::pair<size_t, T>> out;
  out.reserve(v.size());
  for (size_t i = 0; i < v.size(); ++i)
    out.emplace_back(i, v[i]);
  return out;
}

template <class T> std::vector<T> unique(std::vector<T> const &v) {
  auto out = v;
  out.erase(std::unique(out.begin(), out.end(), out.end()));
  return out;
}

template <class A, class B, class C>
std::vector<std::tuple<A, B, C>> zip3(std::vector<A> const &a,
                                      std::vector<B> const &b,
                                      std::vector<C> const &c) {
  auto n = std::min({a.size(), b.size(), c.size()});
  std::vector<std::tuple<A, B, C>> out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i)
    out.emplace_back(a[i], b[i], c[i]);
  return out;
}

template <class A, class B, class C, class F>
auto zip_with3(std::vector<A> const &a, std::vector<B> const &b,
               std::vector<C> const &c, F f) {
  std::vector<std::invoke_result_t<F, A, B, C>> out;
  auto n = std::min({a.size(), b.size(), c.size()});
  out.reserve(n);
  for (size_t i = 0; i < n; ++i)
    out.push_back(f(a[i], b[i], c[i]));
  return out;
}

template <class A, class B>
std::pair<std::vector<A>, std::vector<B>>
unzip(std::vector<std::pair<A, B>> const &ps) {
  std::vector<A> as;
  std::vector<B> bs;
  as.reserve(ps.size());
  bs.reserve(ps.size());
  for (auto const &[a, b] : ps) {
    as.push_back(a);
    bs.push_back(b);
  }
  return {std::move(as), std::move(bs)};
}

template <class T> T sum(std::vector<T> const &v) {
  return std::accumulate(v.begin(), v.end(), T{});
}

template <class T> T product(std::vector<T> const &v) {
  return std::accumulate(v.begin(), v.end(), T{1});
}

template <class T> std::optional<T> maximum(std::vector<T> const &v) {
  if (v.empty())
    return std::nullopt;
  return *std::max_element(v.begin(), v.end());
}

template <class T> std::optional<T> minimum(std::vector<T> const &v) {
  if (v.empty())
    return std::nullopt;
  return *std::min_element(v.begin(), v.end());
}

template <class T, class F>
std::pair<std::vector<T>, std::vector<T>> span(std::vector<T> const &v,
                                               F pred) {
  auto it = std::find_if_not(v.begin(), v.end(), pred);
  return {std::vector<T>(v.begin(), it), std::vector<T>(it, v.end())};
}

template <class T, class F>
std::vector<T> scan(std::vector<T> const &v, T init, F op) {
  std::vector<T> out;
  out.reserve(v.size());
  T acc = init;
  for (auto const &x : v) {
    acc = op(acc, x);
    out.push_back(acc);
  }
  return out;
}

template <class T> std::vector<T> intersperse(std::vector<T> const &v, T sep) {
  std::vector<T> out;
  out.reserve(v.size() * 2);
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) {
      out.push_back(sep);
    }
    out.push_back(v[i]);
  }
  return out;
}

template <class T>
std::vector<T> intercalate(std::vector<std::vector<T>> const &vs,
                           std::vector<T> const &sep) {
  std::vector<T> out;
  for (size_t i = 0; i < vs.size(); ++i) {
    if (i)
      out.insert(out.end(), sep.begin(), sep.end());
    out.insert(out.end(), vs[i].begin(), vs[i].end());
  }
  return out;
}

template <class T> std::vector<T> replicate(size_t n, T x) {
  return std::vector<T>(n, std::move(x));
}

template <class T = int> std::vector<T> range(T from, T to, T step = 1) {
  std::vector<T> out;
  if (step == T{})
    return out;
  for (T i = from; i < to; i += step)
    out.push_back(i);
  return out;
}
} // namespace fp
