#pragma once
#include <memory>
#include <type_traits>
#include <unordered_map>

namespace fp {
template <class Arg, class F> auto memoize(F f) {
  using Ret = std::invoke_result_t<F, Arg>;
  auto cache = std::make_shared<std::unordered_map<Arg, Ret>>();

  return [f, cache](Arg x) {
    auto it = cache->find(x);
    if (it != cache->end())
      return it->second;
    auto r = f(x);
    cache->emplace(x, r);
    return r;
  };
}
} // namespace fp
