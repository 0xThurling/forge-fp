#pragma once
#include "error.hpp"
#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace fp {

// Error / Outcome<T>
inline std::ostream &operator<<(std::ostream &os, Error const &e) {
  return os << to_string(e);
}

template <class T>
std::ostream &operator<<(std::ostream &os, Either<Error, T> const &o) {
  if (o.is_ok())
    return os << "ok(" << o.value() << ")";
  return os << "err(" << o.error() << ")";
}

inline std::ostream &operator<<(std::ostream &os, Either<Error, void> const &o) {
  if (o.is_ok())
    return os << "ok()";
  return os << "err(" << o.error() << ")";
}

// Either<E, T> / Result<T>
template <class E, class T>
std::ostream &operator<<(std::ostream &os, Either<E, T> const &e) {
  if (e.is_ok())
    return os << "ok(" << e.value() << ")";
  return os << "err(" << e.error() << ")";
}

// Either<E, void> / Result<void>
template <class E>
std::ostream &operator<<(std::ostream &os, Either<E, void> const &e) {
  if (e.is_ok())
    return os << "ok()";
  return os << "err(" << e.error() << ")";
}

// Validation<T> — error side is a vector<string>; join the messages
template <class T>
std::ostream &operator<<(std::ostream &os,
                         Either<std::vector<std::string>, T> const &v) {
  if (v.is_ok())
    return os << "ok(" << v.value() << ")";
  os << "err(";
  for (std::size_t i = 0; i < v.error().size(); ++i)
    os << (i ? "; " : "") << v.error()[i];
  return os << ")";
}

} // namespace fp
