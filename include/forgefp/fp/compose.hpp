#pragma once
#include <type_traits>
#include <utility>

namespace fp {
template <class F, class G> auto compose(F f, G g) {
  return [f, g](auto &&...args) {
    return f(g(std::forward<decltype(args)>(args)...));
  };
}

template <class F, class... Fs> auto pipe(F f, Fs... fs) {
  if constexpr (sizeof...(fs) == 0)
    return f;
  else
    return [f, rest = pipe(fs...)](auto &&x) {
      return rest(f(std::forward<decltype(x)>(x)));
    };
}

template <class T> struct Piped {
  T value;
};

template <class T> constexpr Piped<std::decay_t<T>> into(T &&x) {
  return {std::forward<T>(x)};
}

template <class T> T out(Piped<T> p) { return std::move(p.value); }

template <class T, class F> auto operator|(Piped<T> p, F f) {
  using R = std::invoke_result_t<F, T>;
  if constexpr (!std::is_same_v<R, void>) {
    return Piped<R>{f(std::move(p.value))};
  } else {
    f(p.value);
    return Piped<T>{p.value};
  }
}

template <class F> auto tap(F side_effect) {
  return [side_effect](auto &&x) {
    side_effect(x);
    return std::forward<decltype(x)>(x);
  };
}
} // namespace fp
