#pragma once
#include <exception>
#include <utility>

namespace fp {

// Runs `f` when the guard is destroyed unless released first. The callable is
// stored inline (no std::function, no allocation); the guard is movable and
// non-copyable.
template <class F> class ScopeGuard {
public:
  explicit ScopeGuard(F f) noexcept : f_(std::move(f)) {}

  ScopeGuard(ScopeGuard &&other) noexcept
      : f_(std::move(other.f_)), active_(other.active_) {
    other.active_ = false;
  }

  ScopeGuard(ScopeGuard const &) = delete;
  ScopeGuard &operator=(ScopeGuard const &) = delete;
  ScopeGuard &operator=(ScopeGuard &&) = delete;

  ~ScopeGuard() {
    if (active_)
      f_();
  }

  // Cancel the cleanup.
  void release() noexcept { active_ = false; }

private:
  F f_;
  bool active_ = true;
};

// Runs `f` on scope exit, always.
template <class F> auto defer(F f) { return ScopeGuard<F>(std::move(f)); }

// Alias matching the std::experimental name.
template <class F> auto scope_exit(F f) { return ScopeGuard<F>(std::move(f)); }

// Runs `f` on scope exit only if no new exception is in flight.
template <class F> auto scope_success(F f) {
  const int count = std::uncaught_exceptions();
  return defer([f = std::move(f), count]() mutable {
    if (std::uncaught_exceptions() == count)
      f();
  });
}

// Runs `f` on scope exit only if a new exception is in flight.
template <class F> auto scope_fail(F f) {
  const int count = std::uncaught_exceptions();
  return defer([f = std::move(f), count]() mutable {
    if (std::uncaught_exceptions() > count)
      f();
  });
}

} // namespace fp
