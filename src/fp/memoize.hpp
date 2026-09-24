#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace fp {
template <class Arg, class F> auto memoize(F f) {
  using Ret = std::invoke_result_t<F, Arg>;
  auto cache = std::make_shared<std::unordered_map<Arg, Ret>>();

  return [f, cache](Arg x) -> Ret {
    auto it = cache->find(x);
    if (it != cache->end())
      return it->second;
    auto [slot, inserted] = cache->emplace(x, f(x)); // construct in place
    (void)inserted;
    return slot->second;
  };
}

namespace detail {
struct pair_hash {
  template <class A, class B>
  std::size_t operator()(std::pair<A, B> const &p) const {
    std::size_t h1 = std::hash<A>{}(p.first);
    std::size_t h2 = std::hash<B>{}(p.second);
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
  }
};

struct tuple_hash {
  template <class... Ts>
  std::size_t operator()(std::tuple<Ts...> const &t) const {
    std::size_t seed = 0;
    std::apply(
        [&seed](auto const &...xs) {
          ((seed ^= std::hash<std::decay_t<decltype(xs)>>{}(xs) +
                    0x9e3779b9 + (seed << 6) + (seed >> 2)),
           ...);
        },
        t);
    return seed;
  }
};
} // namespace detail

// Two-argument memoization: caches on the pair (A, B).
template <class A, class B, class F> auto memoize2(F f) {
  using Ret = std::invoke_result_t<F, A, B>;
  auto cache = std::make_shared<
      std::unordered_map<std::pair<A, B>, Ret, detail::pair_hash>>();

  return [f, cache](A a, B b) {
    auto key = std::make_pair(a, b);
    auto it = cache->find(key);
    if (it != cache->end())
      return it->second;
    auto r = f(a, b);
    cache->emplace(key, r);
    return r;
  };
}

// N-argument memoization: caches on the tuple (Args...).
template <class... Args, class F> auto memoizeN(F f) {
  using Ret = std::invoke_result_t<F, Args...>;
  auto cache = std::make_shared<
      std::unordered_map<std::tuple<Args...>, Ret, detail::tuple_hash>>();

  return [f, cache](Args... args) {
    auto key = std::make_tuple(args...);
    auto it = cache->find(key);
    if (it != cache->end())
      return it->second;
    auto r = f(args...);
    cache->emplace(key, r);
    return r;
  };
}
} // namespace fp
