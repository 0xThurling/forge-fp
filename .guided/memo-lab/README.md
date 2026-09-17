# Project 5 — Memo Lab (CodeCrafters-style)

Build a dynamic-programming toolkit from `fix` and `memoize`, one stage at a
time. Each stage solves a harder recurrence and ends with a **Verify** check.

**Modules:** `memoize.hpp`, `combinators.hpp`, `curry.hpp`, `vec.hpp`.
**Compile:** `g++ -std=c++20 -I src -o app app.cpp && ./app`

---

## Stage 1 — Recursion needs a name (`fix`)

Goal: write Fibonacci as a lambda that can call itself.

```cpp
#include <fp/combinators.hpp>
#include <iostream>

int main() {
    auto fib = fp::fix([](auto recur, int n) -> long long {
        return n < 2 ? n : recur(n - 1) + recur(n - 2);
    });
    std::cout << fib(10) << "\n";   // 55
}
```

**Verify:** `fib(10) == 55`.

**Concept — the Y combinator.** `fix` hands your function a `recur` callable as
its first argument, so a lambda can recurse without `std::function`.

## Stage 2 — Memoize it (exponential → linear)

Goal: cache results so each subproblem is solved once.

```cpp
#include <fp/all.hpp>

int main() {
    auto fib = fp::memoize<int>(fp::fix([](auto recur, int n) -> long long {
        return n < 2 ? n : recur(n - 1) + recur(n - 2);
    }));
    std::cout << fib(90) << "\n";   // 2880067194370816120 — instant
}
```

**Verify:** `fib(90)` returns quickly (without memoize, `fib(90)` would take
longer than the age of the universe).

**Concept — memoization.** `memoize<Arg>(f)` keys an `unordered_map<Arg, Ret>` on
the single argument. Wrap the *recursive* callable itself, so every sub-call
goes through the cache.

## Stage 3 — Climbing stairs

Goal: `ways(n)` = ways to climb `n` steps taking 1 or 2 at a time.

```cpp
auto ways = fp::memoize<int>(fp::fix([](auto recur, int n) -> long long {
    return n <= 1 ? 1 : recur(n - 1) + recur(n - 2);
}));
// ways(4) == 5: 1111, 112, 121, 211, 22
```

**Verify:** `ways(4) == 5`.

**Concept — the recurrence is the program.** State the recurrence, wrap it in
`fix` + `memoize`, and it's done. No DP table.

## Stage 4 — Coin change: minimum coins

Goal: min coins to make `amount` from `{1,5,10,25}`.

```cpp
constexpr int kCoins[] = {1, 5, 10, 25};

auto min_coins = fp::memoize<int>(fp::fix([](auto recur, int amount) -> int {
    if (amount == 0) return 0;
    int best = 1'000'000;
    for (int c : kCoins)
        if (c <= amount) best = std::min(best, recur(amount - c));
    return best + 1;
}));
// min_coins(41) == 4  (25 + 10 + 5 + 1)
```

**Verify:** `min_coins(41) == 4`.

**Concept — single-argument DP.** With *fixed* denominations, the state is just
the amount, so it fits `memoize<int>` directly.

## Stage 5 — Coin change: number of ways

Goal: *combinations* (not permutations) that sum to `amount`. This needs the
coin *index* too, so encode the pair as one key.

```cpp
auto count_ways = fp::memoize<int>(fp::fix([](auto recur, int state) -> long long {
    int amount = state / 4;      // decode: 4 denominations
    int idx    = state % 4;
    if (amount == 0) return 1;
    if (idx < 0)    return 0;
    long long skip  = recur((amount) * 4 + (idx - 1));   // don't use coin idx
    long long take  = kCoins[idx] <= amount ? recur((amount - kCoins[idx]) * 4 + idx) : 0;
    return skip + take;
}));
// count_ways(5 * 4 + 3) == 2  (five pennies, or one nickel)
```

**Verify:** `count_ways(5 * 4 + 3) == 2`.

**Concept — encoding a pair as a key.** `memoize` keys on one argument; a
two-variable state `(amount, idx)` becomes `amount * K + idx`. This is the
manual DP-table-indexing step, made explicit.

## Stage 6 — Longest increasing subsequence

Goal: length of the longest increasing subsequence ending at index `i`.

```cpp
std::vector<int> seq = {10, 9, 2, 5, 3, 7, 101, 18};

auto lis = fp::memoize<int>(fp::fix([&](auto recur, int i) -> int {
    int best = 1;
    for (int j = 0; j < i; ++j)
        if (seq[j] < seq[i]) best = std::max(best, recur(j) + 1);
    return best;
}));

// overall answer: max over all end indices
int answer = *fp::maximum(fp::map(fp::range(0, (int)seq.size()), lis));
// answer == 4   (2, 3, 7, 101)
```

**Verify:** `answer == 4`.

**Concept — recursion over indices.** The state is the index `i`; `recur(j)`
solves the subproblem. `memoize` dedups the overlapping `recur` calls.

## Stage 7 — Edit distance (two arguments)

Goal: min edits (insert/delete/replace) to turn `a` into `b`. Two indices →
encode `(i, j)` as `i * (m+1) + j`.

```cpp
std::string a = "kitten", b = "sitting";
size_t m = a.size(), n = b.size();

auto dist = fp::memoize<size_t>(fp::fix([&](auto recur, size_t key) -> size_t {
    size_t i = key / (n + 1), j = key % (n + 1);
    if (i == m) return n - j;                 // insert the rest of b
    if (j == n) return m - i;                 // delete the rest of a
    if (a[i] == b[j]) return recur((i + 1) * (n + 1) + (j + 1));
    return 1 + std::min({
        recur(i * (n + 1) + (j + 1)),          // insert
        recur((i + 1) * (n + 1) + j),          // delete
        recur((i + 1) * (n + 1) + (j + 1))});  // replace
}));

// dist(0 * (n+1) + 0) == 3
```

**Verify:** `dist(0) == 3`.

**Concept — two-dimensional state.** The trick from stage 5 generalizes: any
multi-argument DP becomes single-argument by packing the tuple into one key.

## Stage 8 — Curry a parameter, then memoize

Goal: fix a parameter (the coin set, or a capacity) with `curry`, leaving a
single-argument function to memoize.

```cpp
auto min_coins_with = [](std::vector<int> const& coins, int amount) -> int {
    auto solve = fp::memoize<int>(fp::fix([&](auto recur, int n) -> int {
        if (n == 0) return 0;
        int best = 1'000'000;
        for (int c : coins) if (c <= n) best = std::min(best, recur(n - c));
        return best + 1;
    }));
    return solve(amount);
};

auto us_min = fp::curry(min_coins_with)(std::vector<int>{1, 5, 10, 25});
// us_min(41) == 4
```

**Verify:** `us_min(41) == 4`, and `fp::curry(min_coins_with)(std::vector<int>{1,2,5})(9) == 3`.

**Concept — partial application.** `curry(f)(arg1)` produces "f with arg1
fixed". Fix the coin set once, then memoize the single-argument remainder.

---

## 🏆 Extensions

1. **0/1 knapsack** — max value fitting capacity `W` (two-variable: encode
   `(item_index, capacity)` as a key).
2. **Longest common subsequence** — LCS of two strings (same key-encoding).
3. **Rod cutting** — max revenue for a rod of length `n` given per-length prices.
4. **A timing harness** — measure `fib(30)` with and without `memoize` to feel
   the difference (use `std::chrono`).
5. **A `memoize2` helper** — write your own two-argument memoizer (a
   `unordered_map<pair<A,B>, R>` with a custom hash) so you don't hand-encode
   keys.

The goal: internalize *recurrence → memoize* so it becomes mechanical, and
learn the two tricks that make `memoize` general — curry a parameter, or pack
the state into one key.
