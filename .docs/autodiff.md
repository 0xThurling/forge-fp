# Autodiff — `autodiff.hpp`

Forward-mode automatic differentiation with dual numbers: exact derivatives of
a function, no step size, no finite differences.

```cpp
#include <fp/autodiff.hpp>      // opt-in, like simd.hpp — not in all.hpp
```

**Why this exists:** hand-derived gradients are where numerical code goes
wrong, and finite differences (`central_difference`) are only approximate. Dual
numbers give you the *exact* derivative of any composition of the supported
operations in a single forward pass, with no extra machinery.

## The theory: dual numbers

A dual number carries a value and a derivative together:

```text
x = (a, b)      meaning  x = a + b*eps,  where eps^2 = 0
```

Arithmetic follows from `eps^2 = 0`:

```text
(a + b eps) + (c + d eps) = (a + c) + (b + d) eps
(a + b eps) * (c + d eps) = a c + (a d + b c) eps      // product rule falls out
```

So multiplication *is* the product rule, division *is* the quotient rule, and
function composition *is* the chain rule. Seeding the derivative input with
`1` and reading the derivative output gives `f'(x)`.

```text
derivative(f, x)  ==  f(Dual(x, 1)).deriv
```

This is the same Taylor-series truncation that powers every AD system; forward
mode propagates **one derivative per input** alongside the value.

## The API

```cpp
template <class T = double> struct fp::ad::Dual {
  T value{};
  T deriv{};
};

// arithmetic: +, -, *, /, unary -, compound assignment, scalar mixing
// comparisons compare `value` (for branches)

// elementary functions (namespace fp::ad, found by ADL)
exp, log, sqrt, sin, cos, tanh, pow(x, k), relu

template <class F, class T = double> T fp::derivative(F f, T x);
```

`fp::Dual<T>` is an alias for `fp::ad::Dual<T>`; the functions live in
`fp::ad` so they never collide with the named operators in `fp::ops`.

## Worked examples

### 1. A derivative in one line

```cpp
auto f = [](fp::Dual<double> x) { return x * x + 3.0 * x - 5.0; };
fp::derivative(f, 2.0);      // 2*2 + 3 = 7
```

### 2. Elementary functions and the chain rule

```cpp
using fp::ad::exp;
using fp::ad::log;

// d/dx (x * e^x) = e^x (1 + x)
fp::derivative([](fp::Dual<double> x) { return exp(x) * x; }, 1.0);
// 2e

// d/dx log(exp(x)) = 1
fp::derivative([](fp::Dual<double> x) { return log(exp(x)); }, 3.0);  // 1
```

### 3. Branching is allowed

Comparisons compare the value, so `relu` and piecewise functions work:

```cpp
auto softplus = [](fp::Dual<double> x) {
  return fp::ad::relu(x) + fp::ad::log(1.0 + fp::ad::exp(-x));
};
fp::derivative(softplus, 2.0);    // sigmoid-ish: 0.88...
```

(At a kink like `relu(0)` the derivative is the branch taken — the usual
convention.)

### 4. Verifying a hand-derived gradient

```cpp
// Analytic: d/dw (w*x - y)^2 = 2 (w*x - y) x
const double x = 2.0, y = 1.0, w = 0.5;

const double numeric = fp::central_difference([&](double dw) {
  const double p = (w + dw) * x - y;
  return p * p;
}, 0.0);

const double exact = fp::derivative([&](fp::Dual<double> dw) {
  const auto p = (w + dw) * x - y;
  return p * p;
}, 0.0);

EXPECT_NEAR(exact, numeric, 1e-6);
EXPECT_DOUBLE_EQ(exact, 2.0 * (w * x - y) * x);
```

`derivative` is exact (to floating point); `central_difference` is approximate.
Use the first to test the second, and both to test analytic code.

### 5. A tiny gradient check helper

```cpp
template <class F>
bool grad_matches(F f, double x, double analytic, double tol = 1e-6) {
  const double numeric = fp::central_difference(f, x);
  return fp::approx_equal(numeric, analytic, tol);
}
```

## When forward mode is enough

Forward mode costs one pass **per input variable** you want derivatives for. It
is ideal for:

- scalar functions `f: R -> R` (one input, one pass);
- small parameter counts (a handful of inputs);
- unit tests of gradients and Jacobian-vector products.

It is **not** how large networks are trained: with millions of parameters you
want reverse mode (backprop), which computes all parameter gradients in one
backward pass. That is a domain concern and lives in the network layer — this
header deliberately stays forward-only.

## Gotchas

- **Opt-in header.** Include `<fp/autodiff.hpp>` explicitly; it is not in
  `all.hpp`.
- **`fp::ad` functions, not `fp::`.** Use `fp::ad::exp` (or `using`), because
  `fp::exp` is a named operator in `ops.hpp`.
- **Seed with `1`.** `fp::derivative` does that for you; if you construct a
  `Dual` by hand, the derivative you read is relative to that seed.
- **Comparisons ignore the derivative.** `x < y` compares values; branching on
  a dual is fine but the derivative is undefined at the branch point.
- **Non-smooth functions** (`relu`, `abs`) return the subgradient of the branch
  taken — the standard convention, and what training code expects.
- **One input at a time.** For a gradient of `f: R^n -> R`, call `derivative`
  `n` times with different seeds, or use reverse mode.
