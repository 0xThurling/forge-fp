# Project 7 — Render Loop

Build a (simulated) frame loop that touches pixels every frame, using an
**arena** for scratch memory and **SIMD** for the math. This project introduces
**scoped memory** and **vectorized mapping**.

**Modules:** `arena.hpp`, `simd.hpp`, `concurrent.hpp`.
**Compile:** `g++ -std=c++20 -O2 -march=native -pthread -I src -o app app.cpp && ./app`

The "frame" is a vector of pixel values we brighten and sum every tick — a stand-
in for a render/dsp pass that allocates scratch and runs vector math per frame.

---

## Step 1 — Scratch memory per frame (`Arena`)

A frame needs temporary buffers that are thrown away each tick. An arena
allocates with a pointer bump and reclaims with one `reset()` — no `malloc`/`free`.

```cpp
#include <fp/arena.hpp>
#include <iostream>
#include <vector>

int main() {
    fp::Arena frame;                       // reuse this across frames
    std::vector<float> pixels(1024, 1.0f); // the "image"

    // per-frame scratch: a temporary buffer, no heap allocation
    auto* scratch = frame.alloc<float>(pixels.size());
    for (size_t i = 0; i < pixels.size(); ++i)
        scratch[i] = pixels[i] * 2.0f;

    std::cout << "first scratch = " << scratch[0] << "\n";
    frame.reset();                          // reclaim everything for next frame
    std::cout << "used after reset = " << frame.used() << "\n";
}
```

**Concept — bump allocation:** `alloc` advances a pointer; `reset` snaps it back.
The whole frame's temporaries cost one bump and one reset, and (contract note)
`reset` does *not* run destructors — keep payloads trivially destructible.

## Step 2 — Vectorize the math (`simd.hpp`)

`map_inplace` applies a lambda to *whole vectors* (several lanes) at once, and
`reduce` sums across lanes. The lambda receives an `fp::vec<float>`.

```cpp
#include <fp/simd.hpp>
#include <vector>
#include <iostream>

int main() {
    std::vector<float> pixels(1024, 1.0f);

    fp::map_inplace(pixels, [](fp::vec<float> x) { return x * 2.0f + 0.5f; });
    float total = fp::reduce(pixels);

    std::cout << "brightness sum = " << total << "\n";   // 1024 * 2.5
}
```

**Concept — SIMD lambda:** you write the *operation* (`x*2+0.5`) once; the
vector type applies it to all lanes. The scalar tail (elements that don't fill
a full vector) is handled for you. This is the win for math the compiler can't
auto-vectorize (`sqrt`, `exp`, …).

## Step 3 — Parallel + SIMD (`par_map_inplace`)

For a big frame, split the work across a `ThreadPool` *and* use SIMD per chunk.

```cpp
#include <fp/all.hpp>
#include <fp/simd.hpp>
#include <iostream>

int main() {
    std::vector<float> pixels(1 << 20, 1.0f);   // ~1M pixels
    fp::ThreadPool pool(4);

    fp::par_map_inplace(pool, pixels, [](fp::vec<float> x) { return x * 1.1f; });

    std::cout << "first pixel = " << pixels[0] << "\n";   // 1.1
}
```

**Concept — two axes of speed:** `par_map_inplace` splits by thread (each worker
owns a chunk) and each chunk is processed with SIMD. Because the chunks are
disjoint, there's no sharing and no locks — purity is what makes parallel + SIMD
safe.

## Step 4 — A frame loop tying it together

```cpp
#include <fp/all.hpp>
#include <fp/simd.hpp>
#include <iostream>

int main() {
    fp::ThreadPool pool(4);
    std::vector<float> frame(1 << 20, 0.5f);

    for (int tick = 0; tick < 10; ++tick) {
        fp::Arena scratch;

        // "render" pass: brighten, then sum a scratch copy
        fp::par_map_inplace(pool, frame, [](fp::vec<float> x) { return x + 0.1f; });
        auto* tmp = scratch.alloc<float>(frame.size());
        std::copy(frame.begin(), frame.end(), tmp);

        float total = fp::reduce(frame);
        std::cout << "tick " << tick << " brightness " << total << "\n";
    }
}
```

**Concept — a frame is a scope:** the arena lives and dies inside one iteration
(`with_arena` would make that un-escapable), and the math is pure vector/parallel
work. No allocation churn, no shared mutable state.

---

## 🏆 Challenge

Make it a *real* frame pipeline:

1. **A blur / convolution pass** — instead of `x + 0.1`, write a
   `map_inplace_fixed` pass that combines neighboring pixels (hint: a fixed-width
   SIMD vector, or a `zip`/`windows` pass over the row).
2. **`normalize` / `clamp`** — use `fp::normalize` or `fp::clamp_inplace` to keep
   values in range each frame.
3. **SIMD `sqrt`** — brighten with `fp::map_sqrt` (the case where SIMD beats the
   compiler ~3×), and compare the timing mentally against the scalar loop.
4. **Profile the arena** — print `used()` per frame and confirm it stays flat
   (no growth) across thousands of ticks.

**Hints:**
- `fp::map_inplace_fixed<T, N>(v, f)` uses a fixed-width vector with a masked
  tail; `fp::simd_mask<T, N>` builds the mask.
- `fp::normalize(v)` divides by the max absolute value; `fp::clamp_inplace(v, lo, hi)` clamps.
- Keep the arena strictly inside the frame iteration — that's the whole
  contract (see [memory docs](../../.docs/memory.md)).

The goal is a frame loop that allocates nothing per tick (arena), runs vector
math (SIMD), and scales across cores (thread pool) — the three performance tools
working together.
