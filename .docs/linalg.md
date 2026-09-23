# Linear algebra — `linalg.hpp`

Dense matrix/vector math over nested ranges (`std::vector<std::vector<T>>`) and
spans. This is where the *hot kernels* live, so every model, layer, and
training loop calls the same optimized implementation instead of hand-rolling
its own triple loop.

```cpp
#include <fp/linalg.hpp>
```

**Why this exists:** a linear regression, a dense layer, an attention score,
and a PCA all need `matmul`; a solver needs `solve`; every metric needs
`mean`/`variance`. Written once, with the right loop order and the right
pivoting, those are fast and correct. Written per-model, they are where subtle
shape bugs and cache misses accumulate.

## The data shape

`linalg` uses two representations:

- **Matrix**: `std::vector<std::vector<T>>` — row-major, each row contiguous.
  This is the same shape `grid.hpp` uses, so `transpose`, `map2d`,
  `for_each_cell`, and the rest compose directly.
- **Vector**: `std::vector<T>`; reductions take `std::span<T const>` so they
  also accept arrays, `Buffer<T>`, and slices without copying.

Shape preconditions are **documented and asserted in debug** (`assert`), not
returned as `Result`: shape validation belongs at the boundary of your domain
code, where the user-supplied data enters. `solve` is the exception — it
returns `Result` because a singular system is a data outcome, not a bug.

## Products

| Function | Shape |
|---|---|
| `matmul(a, b)` | `(m x k) * (k x n) -> (m x n)` |
| `batched_matmul(a, b)` | `(B x m x k) * (B x k x n) -> (B x m x n)` |
| `matvec(a, x)` | `(m x k) * (k) -> (m)` |
| `outer(a, b)` | `(m) x (n) -> (m x n)` |
| `matmul(a, m, k, b, n)` | flat spans, `(m x k) * (k x n) -> (m x n)` |

```cpp
std::vector<std::vector<int>> a = {{1, 2, 3}, {4, 5, 6}};   // 2x3
std::vector<std::vector<int>> b = {{7, 8}, {9, 10}, {11, 12}}; // 3x2

fp::matmul(a, b);                 // {{58, 64}, {139, 154}}
fp::matvec(a, std::vector<int>{1, 0, -1});  // {-2, -2}
fp::outer(std::vector<int>{1, 2}, std::vector<int>{3, 4, 5});
// {{3, 4, 5}, {6, 8, 10}}
```

When the data already lives in one contiguous buffer (`std::vector`,
`Buffer<T>`, an array, a slice), pass spans and the shape: no pointer chase per
element. Measured at 128³ it is **~2x faster** than the nested form.

```cpp
std::vector<float> A = /* m*k row-major */, B = /* k*n row-major */;
auto C = fp::matmul<float>(A, m, k, B, n);   // vector<float>, m*n row-major
```

### Theory: why the loop order matters

`matmul` computes

```text
C[i][j] = sum_p A[i][p] * B[p][j]
```

The naive order `i, j, p` walks **down a column of `B`** on every step — one
cache miss per multiply for a large matrix, because the column elements are
`n` doubles apart. The `i, k, j` order used here keeps an `A[i][p]` scalar in a
register and walks **along a row of `B`**, which is contiguous, accumulating
into a contiguous row of `C`.

Measured on 256x256 doubles:

| Order | Time |
|---|---|
| naive `i-j-k` | 11.93 ms |
| `fp::matmul` (`i-k-j`) | 6.81 ms (1.75x faster) |

Same answer, different memory traffic.

## Solving systems

```cpp
fp::Result<std::vector<double>> solve(A, b);
```

Solves `A x = b` by Gaussian elimination with **partial pivoting**.

```cpp
// 2x + 3y = 8 ; 5x - y = 3  ->  x = 1, y = 2
auto x = fp::solve({{2.0, 3.0}, {5.0, -1.0}}, {8.0, 3.0});
x.value();       // {1.0, 2.0}

fp::solve({{1.0, 2.0}, {2.0, 4.0}}, {1.0, 2.0});   // err("solve: singular matrix")
```

### Theory: pivoting

Elimination divides by the current pivot `A[col][col]`. If that entry is tiny,
the multiplier `A[r][col] / pivot` explodes and round-off dominates the result.
Partial pivoting swaps in the row with the **largest absolute value** in the
column before dividing, which keeps every multiplier in `[-1, 1]` and the
computation stable. A pivot below `1e-12` means the matrix is (numerically)
singular, and `solve` returns an error rather than a garbage vector.

## Elementwise operations

```cpp
fp::hadamard(a, b);              // elementwise a[i] * b[i]   (same size)
fp::scale(v, 2.0);               // 2 * v
fp::add_row_broadcast(g, row);   // g[i][j] += row[j]         (bias add)
```

```cpp
// The dense layer: y = x W + b, broadcast over the batch.
auto y = fp::add_row_broadcast(fp::matmul(x, W), b);
```

`add_row_broadcast` is `map2d` specialized for the bias pattern; it copies the
grid and adds in one pass.

## Reductions

| Function | Returns |
|---|---|
| `dot(a, b)` | `sum a_i b_i` (spans) |
| `norm_l1(v)` | `sum |v_i|` |
| `norm_l2(v)` | `sqrt(sum v_i^2)` |
| `argmax(r)` / `argmin(r)` | `optional<size_t>`, first on ties |
| `mean(r)` | arithmetic mean |
| `variance(r, ddof = 0)` | population (`0`) or sample (`1`) variance |
| `row_sums(g)` / `col_sums(g)` | per-row / per-column sums |
| `row_means(g)` / `col_means(g)` | per-row / per-column means |
| `argmax_rows(g)` / `argmin_rows(g)` | one index per row |

```cpp
std::vector<double> v = {3.0, 1.0, 4.0, 1.0, 5.0};
fp::dot<double>(v, v);         // 52.0
fp::norm_l1<double>(v);        // 14.0
fp::norm_l2<double>(v);        // sqrt(52)

fp::argmax(std::vector<int>{3, 7, 7, 2});   // optional{1}  (first max)
fp::argmin(std::vector<int>{});             // nullopt (empty)

fp::mean(std::vector<int>{1, 2, 3, 4});         // 2.5
fp::variance(std::vector<int>{1, 2, 3, 4}, 0);  // 1.25  (population)
fp::variance(std::vector<int>{1, 2, 3, 4}, 1);  // 1.667 (sample)
```

`dot`/`norm_*` take spans; when `simd.hpp` is included, `fp::simd::dot` is a
faster overload for full `std::vector`s and wins overload resolution
automatically.

## Worked examples

### 1. Linear regression prediction and gradient

```text
y_hat  = X w + b
grad_w = (2/n) X^T (y_hat - y)
```

```cpp
auto y_hat = fp::matvec(X, w);
fp::transform_inplace(y_hat, [b](double v) { return v + b; });   // scalar bias

auto err = fp::zip_with(y_hat, y, [](double a, double c) { return a - c; });
auto grad_w = fp::matvec(fp::transpose(X), err);
grad_w = fp::scale(grad_w, 2.0 / static_cast<double>(n));
```

### 2. Ridge regression via the normal equations

```text
w = (X^T X + lambda I)^-1 X^T y
```

```cpp
auto xt = fp::transpose(X);
auto xtx = fp::matmul(xt, X);
auto xty = fp::matvec(xt, y);

for (std::size_t i = 0; i < xtx.size(); ++i)
  xtx[i][i] += lambda;                       // ridge term

auto w = fp::solve(xtx, xty);                // Result<vector<double>>
```

### 3. Attention scores

```cpp
// Q: (Tq, d), K: (Tk, d), V: (Tk, d)
auto scores = fp::matmul(Q, fp::transpose(K));     // (Tq, Tk)
fp::softmax_rows(scores);                          // weights sum to 1 per row
auto context = fp::matmul(scores, V);              // (Tq, d)
```

### 4. Multiclass decoding

```cpp
fp::softmax_rows(logits);                   // logits now hold probabilities
auto classes = fp::argmax_rows(logits);     // one index per row
```

### 5. Feature statistics

```cpp
auto means = fp::col_means(X);      // one mean per feature
auto totals = fp::row_sums(X);      // one total per sample
```

## Performance notes

- Results allocate once (the output), inner loops allocate nothing.
- `matmul` is `i-k-j` with a register accumulator; the next step up would be
  blocking/tiling, which is a benchmark-driven change, not a rewrite.
- `matvec` matches a hand-written loop (38.0 vs 38.2 microseconds on 256x256).
- For very large matrices, consider `fp::par_for(pool, 0, m, ...)` to split
  the rows across threads (see [concurrency](concurrency.md)).

## Gotchas

- **Preconditions are asserts.** In release builds, wrong shapes are
  undefined behaviour. Validate at your boundary (or call `solve`, which does
  return `Result`).
- **Nested vectors are not one allocation.** Each row is its own `vector`;
  copying a matrix copies every row. Prefer `const&` and return values.
- **No strided views.** `transpose` copies. Column-wise algorithms use
  `col_sums`/`col_means` (one pass, no transpose) rather than materializing a
  transposed matrix.
- **`variance` needs `n > ddof`** (asserted): sample variance of one sample is
  undefined.
- **`argmax` ties keep the first index**, consistently with `fp::stats` and
  metrics.
- **`dot` on spans needs the type spelled out** (`fp::dot<double>(a, b)`) when
  passing containers, because spans are not deduced from `std::vector`. With
  `simd.hpp` included, `fp::dot(vec, vec)` works directly.
