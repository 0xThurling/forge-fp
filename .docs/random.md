# Random — `random.hpp`

One seedable engine, explicit everywhere, reproducible by construction.

```cpp
#include <fp/random.hpp>
```

**Why this exists:** randomness that is created ad hoc (`std::random_device`
inside a helper, a static engine somewhere) is untestable and
non-reproducible. `fp::Rng` is a value: you construct it with a seed, pass it
by reference, and the same seed produces the same sequence. Library code never
constructs one internally — if a function randomizes, its signature says so.

## The API

```cpp
fp::Rng rng(1234);              // seed

rng.next_u64();                 // raw engine word
rng.randint(1, 6);              // inclusive integer range
rng.uniform(0.0, 1.0);          // real interval [lo, hi)
rng.normal(0.0, 1.0);           // Gaussian
rng.bernoulli(0.3);             // true with probability p
rng.shuffle(v);                 // Fisher-Yates, in place
rng.sample_indices(n, k);       // k distinct indices from [0, n); O(k) when k << n
rng.categorical(weights);       // weighted choice, weights need not sum to 1
rng.weighted_choice(weights);   // alias of categorical
rng.seed();                     // the seed it was constructed with
```

`Rng` wraps `std::mt19937_64` and constructs a fresh distribution per call
(cheap, and avoids carrying stale distribution state).

## Worked examples

### 1. Reproducible train/test split

```cpp
fp::Rng rng(42);

// your domain helper — it takes an Rng& for exactly this reason
auto split = train_test_split(data, 0.2, rng);
```

Run it twice with the same seed and you get the same split; change the seed and
you get a different one. The split helpers live in your domain layer; the only
thing fp supplies is the seeded engine.

### 2. Shuffling and mini-batches

```cpp
std::vector<std::size_t> idx = fp::range<std::size_t>(0, n);
rng.shuffle(idx);

for (std::size_t start = 0; start < n; start += batch) {
  auto end = std::min(n, start + batch);
  auto batch_idx = std::vector<std::size_t>(idx.begin() + start, idx.begin() + end);
  train_on(data.subset(batch_idx));
}
```

### 3. Sampling without replacement

```cpp
auto picked = rng.sample_indices(1000, 10);   // 10 distinct indices
```

Implementation: a Fisher-Yates shuffle truncated to the first `k` positions —
`O(n)` setup, `O(k)` draws, no rejection loop. `k` is clamped to `n`.

### 4. Weighted choice (and LLM-style sampling)

```cpp
std::vector<double> logits = {2.0, 1.0, 0.1};
auto token = rng.categorical(fp::softmax(logits));
```

`categorical` is inverse-CDF sampling over the normalized weights. With
`softmax` it becomes the temperature-free core of token sampling; top-k/top-p
are filters applied to the logits before this call.

### 5. Deterministic weight initialization

```cpp
std::vector<double> w(n);
const double bound = 1.0 / std::sqrt(static_cast<double>(n));
for (double &x : w)
  x = rng.uniform(-bound, bound);
```

### 6. Dropout masks

```cpp
std::vector<double> mask(n);
for (double &m : mask)
  m = rng.bernoulli(keep_prob) ? 1.0 / keep_prob : 0.0;
```

## Theory: PRNGs and distributions

- `std::mt19937_64` is a **Mersenne Twister** with a 64-bit output: fast,
  long period, and *not* cryptographically secure. That is the right trade for
  simulations, ML, and games; never use it for secrets.
- The distributions are transformations of the engine's uniform stream:
  `uniform_int_distribution` rejection-samples to remove modulo bias;
  `normal_distribution` uses the Box-Muller / ziggurat family;
  `categorical` is the inverse CDF.
- **Reproducibility** is guaranteed for a fixed standard library and platform.
  The C++ standard does not pin the *distributions'* algorithms, so bit-exact
  cross-platform sequences are not promised — test structure (permutation,
  bounds, moments), not exact values.

## Gotchas

- **No implicit RNG.** There is no default engine; every randomizing function
  takes `Rng&`. This makes call sites greppable and tests deterministic.
- **Same seed, same sequence** — as long as the platform and library are the
  same. Don't hash the raw words for anything security-related.
- **`categorical` asserts** a non-empty weight vector with a positive total.
  Normalize (or don't — it normalizes internally) but do not pass all zeros.
- **`sample_indices` clamps `k > n`** to `n` instead of looping forever.
- **Distributions are not free.** Creating a `normal_distribution` per call is
  cheap relative to the engine, but in a very hot loop, hoist the `Rng` and
  reuse the same call pattern.
- **`uniform(lo, hi)` is half-open** `[lo, hi)`; `randint(lo, hi)` is closed
  `[lo, hi]`. The difference matters at the edges.
- **One `Rng` per thread.** It holds a `std::mt19937_64` by value and is not
  synchronized; sharing one across threads is a data race. Seed each thread's
  generator differently (e.g. `base_seed + thread_index`).
- **`sample_indices` is O(k) for sparse draws** (rejection sampling into a set)
  and O(n) only when `k >= n/4`, where the Fisher-Yates prefix wins.
