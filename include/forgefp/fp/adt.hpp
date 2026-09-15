#pragma once
#include <utility>
#include <variant>

namespace fp {
template <class... Fs> struct overload : Fs... {
  using Fs::operator()...;
};
template<class... Fs> overload(Fs...) -> overload<Fs...>;

template<class Variant, class... Fs> auto match(Variant &&v, Fs... fs) {
  return std::visit(overload{fs...}, std::forward<Variant>(v));
}
} // namespace fp
