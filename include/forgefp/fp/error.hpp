#pragma once
// Structured errors: an error code, a message, a context chain, and the
// source location where the error was created. `Outcome<T>` is the
// `Either<Error, T>` counterpart of the string-based `Result<T>`.

#include "result.hpp"
#include <source_location>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace fp {

struct Error {
  std::error_code code{};
  std::string message;
  std::vector<Error> causes; // outermost-first chain of nested errors
  std::source_location where = std::source_location::current();

  bool operator==(Error const &o) const {
    return code == o.code && message == o.message && causes == o.causes;
  }

  // Lets `return fp::error("bad");` work in a function returning `Outcome<T>`.
  template <class T> operator Either<Error, T>() const {
    return Either<Error, T>::err(*this);
  }
};

template <class T> using Outcome = Either<Error, T>;

// `error("bad")` captures the call site in `where`.
inline Error error(std::string msg, std::error_code code = {},
                   std::source_location where =
                       std::source_location::current()) {
  return Error{code, std::move(msg), {}, where};
}

// A few standard codes for the common cases.
namespace errc {
inline const std::error_code cancelled =
    std::make_error_code(std::errc::operation_canceled);
inline const std::error_code timeout =
    std::make_error_code(std::errc::timed_out);
inline const std::error_code invalid =
    std::make_error_code(std::errc::invalid_argument);
inline const std::error_code not_found =
    std::make_error_code(std::errc::no_such_file_or_directory);
} // namespace errc

namespace detail {
inline Error to_error(Error const &e) { return e; }
inline Error to_error(std::string const &s) { return error(s); }
inline Error to_error(std::error_code const &c) { return error(c.message(), c); }
} // namespace detail

// Push one level of context: `msg` describes the failed operation, the
// original error becomes its cause. The code is preserved.
template <class T>
Outcome<T> with_context(Outcome<T> const &o, std::string context,
                        std::source_location where =
                            std::source_location::current()) {
  if (o.is_ok())
    return o;
  Error inner = o.error();
  Error outer{inner.code, std::move(context), {}, where};
  outer.causes.push_back(std::move(inner));
  return Outcome<T>::err(std::move(outer));
}

inline Error const &root_cause(Error const &e) {
  return e.causes.empty() ? e : root_cause(e.causes.back());
}

inline std::string to_string(Error const &e) {
  std::string out = e.message;
  for (auto const &c : e.causes)
    out += ": " + to_string(c);
  return out;
}

template <class T> Result<T> to_result(Outcome<T> const &o) {
  if (o.is_ok())
    return ok<T>(o.value());
  return err<T>(to_string(o.error()));
}

inline Result<void> to_result(Outcome<void> const &o) {
  if (o.is_ok())
    return ok<void>();
  return err<void>(to_string(o.error()));
}

template <class T>
Outcome<T> from_result(Result<T> const &r, std::error_code code = {},
                       std::source_location where =
                           std::source_location::current()) {
  if (r.is_ok())
    return Outcome<T>::ok(r.value());
  return Outcome<T>::err(Error{code, r.error(), {}, where});
}

inline Outcome<void> from_result(Result<void> const &r,
                                 std::error_code code = {},
                                 std::source_location where =
                                     std::source_location::current()) {
  if (r.is_ok())
    return Outcome<void>::ok();
  return Outcome<void>::err(Error{code, r.error(), {}, where});
}

} // namespace fp
