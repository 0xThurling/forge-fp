# Numerics — `numerics.hpp`

The numeric functions that every probability model, loss, and training loop
ends up needing — written once, with the stability details handled.

```cpp
#include <fp/numerics.hpp>
```

**Why this exists:** the textbook formulas overflow. `exp(1000)` is `inf`,
`log(0)` is `-inf`, and a softmax written as `exp(x) / sum(exp(x))` produces
`nan` on ordinary logits. Each function here implements the *stable* form and
documents the invariant it preserves, so callers can use the formula they were
taught without rediscovering the tricks.

## Stable nonlinearities

### `sigmoid(z)`

```text
sigma(z) = 1 / (1 + e^-z)
```

Computed with two branches so neither tail overflows:

```text
z >= 0:  e = exp(-z)      -> 1 / (1 + e)
z <  0:  e = exp(z)       -> e / (1 + e)
```

```cpp
fp::sigmoid(0.0);      // 0.5
fp::sigmoid(100.0);    // 1.0 (not inf/inf = nan)
fp::sigmoid(-100.0);   // 0.0
fp::sigmoid(1000.0);   // finite
```

The derivative is `sigma(z) * (1 - sigma(z))` — you already have the value, so
you never need a separate function.

### `relu(z)`

```cpp
fp::relu(-2.0);   // 0.0
fp::relu(2.0);    // 2.0
```

The subgradient at 0 is conventionally 0.

### `clamp(value, lo, hi)`

```cpp
fp::clamp(5, 0, 3);       // 3
fp::clamp(-1.0, 0.0, 1.0); // 0.0
```

(The curried pipeline form is `fp::clamp(lo, hi)(x)` in `ops.hpp` — see
[composition](composition.md).)

## The log-sum-exp trick

Every stable probability routine is built on one identity:

```text
log(sum_i e^{x_i}) = m + log(sum_i e^{x_i - m})     where m = max_i x_i
```

Subtracting the max keeps every exponent in `(-inf, 0]`, so `exp` cannot
overflow, and the result is mathematically identical.

### `logsumexp(r)`

```cpp
fp::logsumexp(std::vector<double>{0.0});          // 0.0
fp::logsumexp(std::vector<double>{1000.0, 1000.0}); // 1000 + log 2
fp::logsumexp(std::vector<double>{});             // -inf (empty)
```

### `softmax(r)` -> `std::vector<double>`

```text
softmax(x)_i = e^{x_i - m} / sum_j e^{x_j - m}
```

```cpp
auto p = fp::softmax(std::vector<double>{1.0, 2.0, 3.0});
// p sums to 1, and p is monotone in the input
auto q = fp::softmax(std::vector<double>{1000.0, 1000.0});  // {0.5, 0.5}
```

Invariants:

- every output is in `[0, 1]`;
- the outputs sum to 1 (up to rounding);
- the result is invariant to adding a constant to every input;
- an empty range yields an empty vector; all `-inf` inputs yield zeros.

### `softmax_rows(g)` — in place

Row-wise softmax over a `vector<vector<T>>` without allocating a second grid:

```cpp
std::vector<std::vector<double>> logits = {
    {1.0, 2.0, 3.0},
    {0.0, 0.0},
};
fp::softmax_rows(logits);
// logits[0] sums to 1; logits[1] == {0.5, 0.5}
```

This is the attention-weights shape: one row per query, softmax over keys.
Floating point only (constrained by `std::floating_point`).

### `log_softmax(r)`

```text
log_softmax(x)_i = x_i - logsumexp(x)
```

Never compute `log(softmax(x))`: the intermediate probabilities underflow to 0
for very negative logits, and `log(0)` is `-inf`. `log_softmax` keeps the
values in log-space the whole way:

```cpp
auto lp = fp::log_softmax(std::vector<double>{1.0, 2.0, 3.0});
// exp(lp) == softmax({1,2,3})
```

This is the function cross-entropy should call; the loss is
`-log_softmax(logits)[target]`.

## Generation

### `linspace(from, to, n)`

`n` points from `from` to `to`, **both endpoints included**:

```cpp
fp::linspace(0.0, 1.0, 3);   // {0.0, 0.5, 1.0}
fp::linspace(5.0, 5.0, 1);   // {5.0}
fp::linspace(0.0, 1.0, 0);   // {} (empty)
```

The last element is assigned exactly (not accumulated), so `linspace(a, b, n)
.back() == b` holds bit-for-bit.

### `arange(from, to, step)` — half-open

```cpp
fp::arange(0.0, 1.0, 0.25);    // {0.0, 0.25, 0.5, 0.75}
fp::arange(3.0, 0.0, -1.0);    // {3.0, 2.0, 1.0}
fp::arange(0.0, 1.0, 0.0);     // {} (zero step is empty, not infinite)
```

`linspace` when you know the count; `arange` when you know the step.

## Comparison and stability guards

### `approx_equal(a, b, eps)`

```text
|a - b| <= eps * (1 + max(|a|, |b|))
```

Relative at scale, absolute near zero — the combination that behaves for both
`1e-9` and `1e9`:

```cpp
fp::approx_equal(1.0, 1.0 + 1e-12);   // true
fp::approx_equal(1.0, 1.1);           // false
fp::approx_equal(1e9, 1e9 + 1.0);     // true (relative)
```

Use it in tests and assertions; never compare floats with `==`.

### `is_finite(x)` and `nan_to_num(x, replacement = 0.0)`

Training loops get one `nan` and stay `nan` forever. Guard the loss, not every
operation:

```cpp
if (!fp::is_finite(loss))
  return fp::fail("loss became non-finite");

w = fp::nan_to_num(w, 0.0);    // replace nan/inf with a safe value
```

`nan_to_num` also replaces `±inf` (anything not finite).

## `central_difference(f, x, h = 1e-6)`

The finite-difference derivative, the primitive behind gradient checking:

```text
f'(x) ~= (f(x + h) - f(x - h)) / (2h)
```

```cpp
auto f = [](double x) { return x * x; };
fp::central_difference(f, 3.0);    // ~6.0
```

### Theory: why central, and why `h = 1e-6`

- A **forward** difference `(f(x+h) - f(x))/h` has error `O(h)` from truncating
  the Taylor series.
- The **central** difference cancels the first-order term and has error
  `O(h^2)` — much more accurate for the same `h`.
- Round-off error grows like `eps/h` (subtracting two nearly equal values).
  The sweet spot balances the two: `h ~ eps^(1/3)`, which for `double` is about
  `6e-6` — hence the default.

Use it to verify analytic gradients:

```cpp
double numeric = fp::central_difference([&](double x) {
  return loss(w0 + x, w1);        // perturb one parameter
}, 0.0);
EXPECT_NEAR(numeric, analytic_grad_w0, 1e-6);
```

For forward-mode automatic differentiation (exact derivatives, no `h`), see
[autodiff](autodiff.md).

## Performance

`softmax`, `softmax_rows`, `log_softmax` and `logsumexp` are dominated by
`std::exp` (~5.8ns per element; a 1M-element softmax takes ~5.8ms). The loops
around it are already at memory bandwidth. If softmax is your hot path, use the
opt-in SIMD tier: `fp::simd::map_exp` vectorizes `exp` (measured ~3x faster than
scalar `std::exp`) and pairs with the same two-pass structure.

## Gotchas

- **`softmax` of all `-inf`** returns zeros (the sum is 0); it does not produce
  `nan`. Decide at your boundary whether that is meaningful.
- **`logsumexp` of an empty range** is `-inf`, not an error.
- **`linspace` includes both endpoints**; `arange` does not include `to`.
  Off-by-one here is a classic bug — pick by whether you know `n` or the step.
- **`approx_equal` is symmetric and scale-aware**, but it is not a
  ULP comparison; for bit-exact round-trip tests compare the bits.
- **`central_difference` evaluates `f` twice** per call; for expensive `f`,
  prefer `fp::ad::derivative` (one forward pass) or analytic gradients.
- **`softmax_rows` needs floating point.** It is constrained so an integral
  grid is a compile error rather than silently truncated probabilities.
