#pragma once
// Forward-mode automatic differentiation. Opt-in, like simd.hpp: include it
// explicitly. Reverse-mode parameter graphs stay in the domain layer.
#include <cmath>
#include <concepts>
#include <type_traits>

namespace fp {
namespace ad {

// Dual numbers: value + derivative. The elementary functions live in this
// namespace so they are found by ADL for `Dual` arguments and never collide
// with fp::ops' named operators.
template <class T = double> struct Dual {
  T value{};
  T deriv{};

  Dual() = default;
  Dual(T v) : value(v), deriv(T{}) {}
  Dual(T v, T d) : value(v), deriv(d) {}

  friend Dual operator+(Dual a, Dual b) {
    return {a.value + b.value, a.deriv + b.deriv};
  }
  friend Dual operator-(Dual a, Dual b) {
    return {a.value - b.value, a.deriv - b.deriv};
  }
  friend Dual operator*(Dual a, Dual b) {
    return {a.value * b.value, a.deriv * b.value + a.value * b.deriv};
  }
  friend Dual operator/(Dual a, Dual b) {
    return {a.value / b.value,
            (a.deriv * b.value - a.value * b.deriv) / (b.value * b.value)};
  }
  Dual operator-() const { return {-value, -deriv}; }

  Dual &operator+=(Dual b) {
    value += b.value;
    deriv += b.deriv;
    return *this;
  }
  Dual &operator-=(Dual b) {
    value -= b.value;
    deriv -= b.deriv;
    return *this;
  }
  Dual &operator*=(Dual b) {
    *this = *this * b;
    return *this;
  }
  Dual &operator/=(Dual b) {
    *this = *this / b;
    return *this;
  }

  friend Dual operator+(Dual a, T s) { return {a.value + s, a.deriv}; }
  friend Dual operator+(T s, Dual a) { return {s + a.value, a.deriv}; }
  friend Dual operator-(Dual a, T s) { return {a.value - s, a.deriv}; }
  friend Dual operator-(T s, Dual a) { return {s - a.value, -a.deriv}; }
  friend Dual operator*(Dual a, T s) { return {a.value * s, a.deriv * s}; }
  friend Dual operator*(T s, Dual a) { return {a.value * s, a.deriv * s}; }
  friend Dual operator/(Dual a, T s) { return {a.value / s, a.deriv / s}; }
  friend Dual operator/(T s, Dual a) {
    return {s / a.value, -(s * a.deriv) / (a.value * a.value)};
  }

  friend bool operator<(Dual const &a, Dual const &b) {
    return a.value < b.value;
  }
  friend bool operator>(Dual const &a, Dual const &b) {
    return a.value > b.value;
  }
  friend bool operator<=(Dual const &a, Dual const &b) {
    return a.value <= b.value;
  }
  friend bool operator>=(Dual const &a, Dual const &b) {
    return a.value >= b.value;
  }
  friend bool operator==(Dual const &a, Dual const &b) {
    return a.value == b.value;
  }
};

template <class T> Dual<T> exp(Dual<T> x) {
  const T e = std::exp(x.value);
  return {e, e * x.deriv};
}

template <class T> Dual<T> log(Dual<T> x) {
  return {std::log(x.value), x.deriv / x.value};
}

template <class T> Dual<T> sqrt(Dual<T> x) {
  const T s = std::sqrt(x.value);
  return {s, x.deriv / (T{2} * s)};
}

template <class T> Dual<T> sin(Dual<T> x) {
  return {std::sin(x.value), std::cos(x.value) * x.deriv};
}

template <class T> Dual<T> cos(Dual<T> x) {
  return {std::cos(x.value), -std::sin(x.value) * x.deriv};
}

template <class T> Dual<T> tanh(Dual<T> x) {
  const T t = std::tanh(x.value);
  return {t, (T{1} - t * t) * x.deriv};
}

template <class T> Dual<T> pow(Dual<T> x, T k) {
  return {std::pow(x.value, k),
          k * std::pow(x.value, k - T{1}) * x.deriv};
}

template <class T> Dual<T> relu(Dual<T> x) {
  return x.value > T{} ? Dual<T>{x.value, x.deriv} : Dual<T>{T{}, T{}};
}

} // namespace ad

template <class T = double> using Dual = ad::Dual<T>;

// Derivative of f at x, seeded with deriv = 1.
template <class F, class T = double> T derivative(F f, T x) {
  return f(ad::Dual<T>(x, T{1})).deriv;
}

} // namespace fp
