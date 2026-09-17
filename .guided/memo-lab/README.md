# Project 5 — Memo Lab

Build a small toolkit for **dynamic programming** using `memoize` and `fix`.
This project introduces **recursion without a name**, **memoization**, and
**higher-order functions**.

**Modules:** `memoize.hpp`, `combinators.hpp`, `curry.hpp`.
**Compile:** `g++ -std=c++20 -I src -o app app.cpp && ./app`

---

## Step 1 — Recursion needs a name (`fix`)

A lambda can't call itself (it has no name). `fix` gives it one: it hands your
function a `recur` callable as its first argument.

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

**Concept — the Y combinator:** `fix` is the Y combinator. It lets you write
recursion *without* `std::function` plumbing. `recur` is your function's own
name, provided at call time.

## Step 2 — Memoize it (exponential → linear)

`fib` above is exponential: `fib(40)` recomputes the same subproblems a
billion times. `memoize` caches results, turning it linear.

```cpp
#include <fp/all.hpp>
#include <iostream>

int main() {
    auto fib = fp::memoize<int>(fp::fix([](auto recur, int n) -> long long {
        return n < 2 ? n : recur(n - 1) + recur(n - 2);
    }));
    std::cout << fib(90) << "\n";   // 2880067194370816120 — instant now
}
```

**Concept — memoization:** trade memory for time. Each distinct input is
computed once; the rest are cache hits. `memoize<Arg>(f)` keys a
`unordered_map<Arg, Ret>` on the single argument. The trick: `memoize` wraps the
*recursive* callable itself, so every sub-call goes through the cache.

## Step 3 — Coin change (min coins)

With fixed denominations, "min coins to make amount `n`" is a **single-argument**
DP, so it fits `memoize<int>` directly.

```cpp
#include <fp/all.hpp>
#include <iostream>
#include <limits>

constexpr int kCoins[] = {1, 5, 10, 25};

int main() {
    auto min_coins = fp::memoize<int>(fp::fix([](auto recur, int amount) -> int {
        if (amount == 0) return 0;
        int best = std::numeric_limits<int>::max();
        for (int c : kCoins)
            if (c <= amount) best = std::min(best, recur(amount - c));
        return best == std::numeric_limits<int>::max() ? best : best + 1;
    }));

    std::cout << "min coins for 41 = " << min_coins(41) << "\n";   // 4 (25+10+5+1)
}
```

**Concept — DP is recursion + memoization:** the recurrence (`1 + min(recur(n-c))`)
is the *recursive* formulation of the problem; `memoize` makes it efficient. You
never write the DP table by hand.

## Step 4 — Curry to fix a parameter, then memoize

Some DP problems have a parameter you want to *fix* (e.g. the coin set, or a
capacity). `curry` binds it first, leaving a single-argument function to
memoize.

```cpp
#include <fp/all.hpp>
#include <iostream>

// min coins for `amount` given a coin *set* (multi-argument recurrence)
int main() {
    auto min_coins_with = [](std::vector<int> const& coins, int amount) -> int {
        // a fresh memoized, single-arg solver per coin set
        auto solve = fp::memoize<int>(fp::fix([&](auto recur, int n) -> int {
            if (n == 0) return 0;
            int best = 1000000;
            for (int c : coins) if (c <= n) best = std::min(best, recur(n - c));
            return best + 1;
        }));
        return solve(amount);
    };

    auto us_min = fp::curry(min_coins_with)(std::vector<int>{1, 5, 10, 25});
    std::cout << us_min(41) << "\n";   // 4
}
```

**Concept — partial application:** `curry(f)(arg1)` produces "f with `arg1`
already filled in". You fix the coin set once, then treat the result as a
single-argument function. This is how multi-argument problems become
single-argument ones that `memoize` can handle.

---

## 🏆 Challenge

Implement one (or more) of these from scratch, memoizing along the way:

1. **Climbing stairs** — `ways(n)` = number of ways to climb `n` steps taking 1
   or 2 at a time. (`ways(0) = 1`, `ways(n) = ways(n-1) + ways(n-2)`.)
2. **Count-the-ways coin change** — `ways(amount)` = number of *combinations* of
   `{1,5,10,25}` that sum to `amount` (this needs the coin *index* too — see
   hint below).
3. **Longest increasing subsequence** — length of the longest strictly
   increasing subsequence of a vector (hint: a recursion on the *index*).
4. **Knapsack (0/1)** — max value fitting a capacity `W`, given weights/values
   (the classic two-parameter DP — curry the capacity *or* the item index).

**Hints:**
- For two-parameter problems, the recurrence is `f(i, j)`. To use `memoize<int>`,
  encode the pair as a single key (e.g. `i * (j_max + 1) + j`) or curry one
  parameter (as in Step 4) — note `memoize` keys on *one* argument.
- `fix` hands you `recur`; call it on the *smaller* subproblem, then combine.
- `fp::maximum(v)` / `fp::minimum(v)` return `optional` — deref with `*` or
  `value_or`.

The goal is to *feel* the recursion → memoization transformation: write the
recurrence first, then add `memoize` and watch it go from exponential to
polynomial.
