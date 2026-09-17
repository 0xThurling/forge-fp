# Project 7 — Render Loop (CodeCrafters-style)

Build a (simulated) frame loop that touches a buffer every tick, using an
**arena** for scratch memory and **SIMD** for the math, then parallelize it.
Each stage adds a performance tool and ends with a **Verify** check.

**Modules:** `arena.hpp`, `simd.hpp`, `concurrent.hpp`.
**Compile:** `g++ -std=c++20 -O2 -march=native -pthread -I src -o app app.cpp && ./app`

---

## Stage 1 — Scratch memory with `Arena`

Goal: allocate per-frame temporaries with a pointer bump, reclaim with `reset()`.

```cpp
#include <fp/arena.hpp>
#include <iostream>

int main() {
    fp::Arena frame;
    auto* scratch = frame.alloc<float>(1024);
    for (size_t i = 0; i < 1024; ++i) scratch[i] = 1.0f;
    std::cout << frame.used() << "\n";   // 4096 bytes
    frame.reset();
    std::cout << frame.used() << "\n";   // 0
}
```

**Verify:** prints `4096` then `0`.

**Concept — bump allocation.** `alloc` advances a pointer; `reset` snaps it back.
A whole frame's temporaries cost one bump and one reset — no `malloc`/`free`.

## Stage 2 — Scope it with `with_arena`

Goal: guarantee the arena can't outlive its scope.

```cpp
double total = fp::with_arena(1024, [](fp::Arena& a) {
    auto* p = a.alloc<double>(4);
    p[0] = 1; p[1] = 2; p[2] = 3; p[3] = 4;
    return p[0] + p[1] + p[2] + p[3];   // return a *value*, not a pointer
});
// total == 10.0
```

**Verify:** `total == 10.0`.

**Concept — scoping enforces the contract.** The arena dies with the lambda, so
you *physically cannot* leak an arena pointer. Return values, not pointers.

## Stage 3 — Vectorize the math with `map_inplace`

Goal: apply an operation to whole vectors at once.

```cpp
#include <fp/simd.hpp>
#include <vector>
#include <iostream>

int main() {
    std::vector<float> pixels(1024, 1.0f);
    fp::map_inplace(pixels, [](fp::vec<float> x) { return x * 2.0f + 0.5f; });
    std::cout << pixels[0] << "\n";   // 2.5
}
```

**Verify:** prints `2.5`.

**Concept — a SIMD lambda.** You write the *operation* once; `fp::vec<float>`
applies it to all lanes. The scalar tail is handled for you.

## Stage 4 — Reductions with `reduce` and `dot`

Goal: sum lanes, and dot two vectors.

```cpp
std::vector<double> a = {1, 2, 3, 4};
std::vector<double> b = {4, 5, 6, 7};
std::cout << fp::reduce(a) << "\n";        // 10
std::cout << fp::dot(a, b) << "\n";        // 1*4+2*5+3*6+4*7 = 60
```

**Verify:** prints `10` then `60`.

**Concept — horizontal combine.** `reduce`/`dot` sum across lanes after the
vector work, matching the scalar formula.

## Stage 5 — Math functions (the real SIMD win)

Goal: `sqrt`/`exp` — the cases the compiler *can't* auto-vectorize.

```cpp
std::vector<double> v(1 << 20, 2.0);
fp::map_sqrt(v);          // SIMD sqrtpd — ~3x faster than a scalar sqrt loop
fp::map_exp(v);           // SIMD exp
```

**Verify:** `v[0]` is `sqrt(2)` (then `exp` of that). Check the value is sane
(`> 1`).

**Concept — where SIMD pays off.** The compiler vectorizes `x*2+1` on its own,
but not `std::sqrt` (an opaque libm call). SIMD intrinsics are the win there.

## Stage 6 — Clamp, normalize, threshold

Goal: range helpers.

```cpp
fp::clamp_inplace(v, 0.0, 1.0);            // [0, 1]
fp::normalize(v);                          // divide by max |x|
fp::threshold_inplace(v, 0.1, 0.9, 0.5);   // out-of-range -> 0.5
```

**Verify:** after `clamp_inplace` on `{-1, 0.5, 2}`, the vector is `{0, 0.5, 1}`.

**Concept — common DSP passes, named.** Each is a `map_inplace` with a specific
lambda; naming them keeps frame code declarative.

## Stage 7 — Parallel + SIMD with `par_map_inplace`

Goal: split the buffer across a thread pool, SIMD per chunk.

```cpp
#include <fp/all.hpp>
#include <fp/simd.hpp>

std::vector<float> pixels(1 << 20, 1.0f);
fp::ThreadPool pool(4);
fp::par_map_inplace(pool, pixels, [](fp::vec<float> x) { return x * 1.1f; });
```

**Verify:** `pixels[0] == 1.1f`.

**Concept — two axes of speed.** Each worker owns a disjoint chunk (no locks),
and each chunk is processed with SIMD. Purity makes parallel + SIMD safe.

## Stage 8 — A full frame loop

Goal: arena + SIMD + parallelism in one tick.

```cpp
#include <fp/all.hpp>
#include <fp/simd.hpp>
#include <iostream>

int main() {
    fp::ThreadPool pool(4);
    std::vector<float> frame(1 << 20, 0.5f);

    for (int tick = 0; tick < 10; ++tick) {
        fp::Arena scratch;
        fp::par_map_inplace(pool, frame, [](fp::vec<float> x) { return x + 0.1f; });

        auto* tmp = scratch.alloc<float>(frame.size());
        std::copy(frame.begin(), frame.end(), tmp);   // scratch copy

        float total = fp::reduce(frame);
        std::cout << "tick " << tick << " brightness " << total << "\n";
    }
}
```

**Verify:** prints 10 lines; brightness grows linearly.

**Concept — a frame is a scope.** The arena lives inside one iteration; the math
is pure vector/parallel work. No allocation churn, no shared mutable state.

## Stage 9 — Fixed-width with a masked tail

Goal: use `map_inplace_fixed` (fixed lane count, masked tail) instead of
`native_simd`.

```cpp
fp::map_inplace_fixed<float, 8>(frame, [](auto x) { return x + 1.0f; });
```

**Verify:** `frame[0]` is `1.0f` larger than before.

**Concept — explicit width.** Fixed-width SIMD gives a predictable lane count and
a masked tail; `native_simd` adapts to the CPU. Pick per need.

## Stage 10 — Benchmark scalar vs SIMD

Goal: measure the `map_sqrt` win to confirm the theory.

```cpp
#include <chrono>
// scalar: for (auto& x : v) x = std::sqrt(x);
// simd:   fp::map_sqrt(v);
// time both over 1<<22 elements; SIMD should be ~3x faster.
```

**Verify:** SIMD `map_sqrt` is meaningfully faster than the scalar `std::sqrt`
loop (and `map_inplace` on `x*2+1` is roughly a tie — the compiler already
vectorizes that one).

**Concept — measure, don't assume.** The win is real for math functions, absent
for patterns the compiler already handles. Benchmark to know which is which.

---

## 🏆 Extensions

1. **A blur pass** — convolve each pixel with its neighbors (use a fixed-width
   SIMD vector, or `fp::windows` over the row).
2. **`normalize` per frame** — keep values bounded across thousands of ticks.
3. **A `gather` pass** — reorder pixels by an index vector (`fp::gather`).
4. **Profile the arena** — print `used()` per frame and confirm it stays flat.
5. **Different data types** — run the loop on `double` and `float` and compare
   the lane width / speed.

The goal: a frame loop that allocates nothing per tick (arena), runs vector math
(SIMD), and scales across cores (thread pool) — the three performance tools
working together.
