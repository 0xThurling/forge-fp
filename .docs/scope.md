# Scopes — `scope.hpp`

Every non-trivial function has cleanup: free a buffer, release a lock, roll
back a partial write, close a handle, restore a state. C++ already has the tool
for this — destructors — but writing a small RAII *type* for a one-off cleanup
is noise. `scope.hpp` gives you the destructor as a **value**: pass a lambda,
get guaranteed cleanup on every exit path, including exceptions.

```cpp
#include <fp/scope.hpp>
```

**Why this exists:** the alternative is duplicating cleanup at every `return`,
wrapping the body in `try`/`catch` just to run one line, or (worse) forgetting
one path. A scope guard puts the cleanup next to the acquisition, where it
belongs, and the compiler enforces the rest. The callable is stored inline —
there is no `std::function`, no allocation, and no virtual call.

## The API

```cpp
auto guard = fp::defer([&] { cleanup(); });   // runs at scope exit, always
```

| Function | Runs when |
|---|---|
| `fp::defer(f)` / `fp::scope_exit(f)` | the scope exits, always |
| `fp::scope_success(f)` | the scope exits with no new exception in flight |
| `fp::scope_fail(f)` | the scope exits because of an exception |

All three return a `ScopeGuard<F>`:

```cpp
template <class F> class ScopeGuard {
public:
  explicit ScopeGuard(F f) noexcept;
  ScopeGuard(ScopeGuard &&) noexcept;      // movable
  ~ScopeGuard();                           // calls f() unless released
  void release() noexcept;                 // cancel the cleanup
};
```

Guards are **movable and non-copyable**. Moving transfers the obligation: only
the final owner runs the cleanup, exactly once.

```cpp
{
  auto a = fp::defer([]{ log("a"); });
  auto b = std::move(a);      // `a` is now inert, `b` will run the lambda
}                             // prints "a" once
```

## The theory: destructors are the only reliable "always"

C++ guarantees destructors run on every exit path — normal return, `return`
from the middle, `break`, and stack unwinding during an exception. No other
construct has that guarantee. A scope guard simply moves your cleanup *into* a
destructor without you having to name a type.

`scope_success`/`scope_fail` need one subtle tool: **counting** uncaught
exceptions.

```cpp
// std::uncaught_exceptions() returns how many exceptions are currently in flight
auto guard = fp::scope_fail([&] { rollback(); });
```

Comparing the count at construction with the count at destruction distinguishes
three cases:

| At construction | At destruction | Meaning |
|---|---|---|
| 0 | 0 | normal exit -> `scope_success` runs, `scope_fail` doesn't |
| 0 | 1 | this scope threw -> `scope_fail` runs, `scope_success` doesn't |
| 1 | 1 | an *outer* scope is unwinding -> neither runs (this scope didn't fail) |

The last row is why the comparison is a count and not `std::uncaught_exception()`
(the C++17-and-earlier function): a guard inside a destructor during unwinding
must not treat someone else's exception as its own failure.

## Worked examples

### 1. Resource cleanup on every exit path

```cpp
fp::Result<Report> analyze(std::string const &path) {
  auto text = fp::read_file(path);
  if (!text.is_ok())
    return fp::fail(text.error());

  auto *handle = open_native(text.value());
  auto guard = fp::defer([&] { close_native(handle); });

  auto parsed = parse(text.value());
  if (!parsed.is_ok())
    return fp::fail(parsed.error());     // handle is closed on the way out

  return fp::ok(build_report(parsed.value()));
}
```

There is exactly one cleanup line and it cannot be skipped.

### 2. Arena checkpoints

`with_arena_scope` is built from `defer` — the checkpoint is restored even if
the callback throws:

```cpp
template <class F> auto with_arena_scope(Arena &a, F f) {
  const std::size_t mark = a.mark();
  auto guard = defer([&a, mark] { a.reset_to(mark); });
  return f(a);                            // value returned before the guard runs
}
```

### 3. Rollback with `scope_fail`

```cpp
bool commit_transaction(Store &store) {
  store.begin();
  auto rollback = fp::scope_fail([&] { store.rollback(); });

  if (!store.write_batch())
    throw std::runtime_error("write failed");   // rollback runs

  store.commit();
  rollback.release();                            // success: cancel the rollback
  return true;
}
```

### 4. Success-only bookkeeping

```cpp
fp::Result<Model> train(Dataset const &data) {
  auto started = fp::now_seconds();
  auto guard = fp::scope_success([&] {
    metrics::record("train_seconds", fp::elapsed_seconds(started));
  });

  return fit(data);          // the metric is recorded only on success
}
```

### 5. Locks, with ordering

Guards run in **reverse order of construction** (LIFO), like local variables:

```cpp
std::mutex a, b;
a.lock();
auto unlock_a = fp::defer([&] { a.unlock(); });
b.lock();
auto unlock_b = fp::defer([&] { b.unlock(); });
// ... b is unlocked first, then a — matching the lock order
```

For plain locks, `std::lock_guard` is already the right tool; scope guards are
for the cases the standard library doesn't have a type for.

### 6. Restoring temporary state

```cpp
void with_locale(std::locale const &loc, std::function<void()> body) {
  auto previous = std::locale::global(loc);
  auto restore = fp::defer([&] { std::locale::global(previous); });
  body();
}
```

## When *not* to use a scope guard

- If the standard library already has the RAII type, use it: `std::lock_guard`,
  `std::unique_ptr`, `std::fstream`, `fp::Buffer`, `fp::Box`.
- If the cleanup is the *only* thing the object does and it lives in several
  functions, give it a named type — names beat comments.
- If the cleanup can throw, a guard is the wrong tool (see below).

## Gotchas

- **A throwing cleanup calls `std::terminate`.** The destructor is implicitly
  `noexcept`; a guard must not throw. Wrap risky cleanup in `try`/`catch`
  yourself.
- **Don't return references into the scope.** A guard that frees a buffer does
  not extend the buffer's lifetime for the caller. Return values.
- **Moved-from guards are inert.** After `auto b = std::move(a);` only `b`
  runs. Don't use `a` afterwards expecting cleanup.
- **`release()` is permanent.** There is no "re-arm"; construct a new guard.
- **Order matters.** Guards run LIFO; declare them after the resource they
  guard and before anything that might exit early.
- **`scope_success`/`scope_fail` only see exceptions**, not `Result` errors. If
  you return `fp::err(...)`, neither runs — the function *succeeded* at
  returning a value. Use `defer` when both paths need the same cleanup.
