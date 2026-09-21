# Time — `time.hpp`

Monotonic timing for steps, frames, and benchmarks.

```cpp
#include <fp/time.hpp>
```

**Why this exists:** timing code usually reaches for `std::chrono` directly,
which means every call site repeats the clock choice and the
duration conversion. Worse, wall-clock time can jump backwards (NTP, DST,
manual clock changes), so elapsed times computed from it can be negative or
wildly wrong. `fp::time` uses `std::chrono::steady_clock` — monotonic by
definition — and hands you seconds as a `double`.

## The API

```cpp
double fp::now_seconds();                 // monotonic seconds since an epoch
double fp::elapsed_seconds(double since); // now_seconds() - since

fp::Stopwatch sw;
sw.elapsed();   // seconds since construction or the last reset()
sw.lap();       // seconds since the last lap (or reset), then starts a new lap
sw.reset();     // restart both clocks
```

`Stopwatch` is a small value type: no threads, no allocation, no I/O.

## Worked examples

### 1. Per-step training log

```cpp
fp::Stopwatch step;

for (std::size_t i = 0; i < steps; ++i) {
  train_one_batch();
  const double seconds = step.lap();
  logger.info("step " + std::to_string(i) + " took " +
              fp::str::to_string(seconds, 4) + "s");
}
```

### 2. Frame timing

```cpp
fp::Stopwatch frame;

while (running) {
  const double dt = frame.lap();      // seconds since the previous frame
  update(static_cast<float>(dt));
  render();
}
```

`dt` is exactly what a fixed-timestep loop needs, and it can never be negative.

### 3. Timing an arbitrary section

```cpp
const double t0 = fp::now_seconds();
auto result = expensive();
metrics::record("expensive_seconds", fp::elapsed_seconds(t0));
```

### 4. Benchmark harness

```cpp
fp::Stopwatch sw;
for (int rep = 0; rep < reps; ++rep)
  work();
const double per_op = sw.elapsed() / reps;
```

## Theory: monotonic vs wall clock

- `system_clock` answers "what time is it?" — it can move backwards.
- `steady_clock` answers "how much time has passed?" — it only moves forward,
  and it is the only correct clock for durations.

Because of that, `fp::time` deliberately provides **no calendar, date, or
time-zone functionality**: those are wall-clock concerns and belong to
`<chrono>`/`<ctime>` at the application edge, not in a timing utility.

## Gotchas

- **`now_seconds()` has an arbitrary epoch.** Only differences are meaningful;
  never print it as a date.
- **Resolution varies.** On most platforms `steady_clock` is nanosecond-ish,
  but very short intervals can measure as 0. Time enough repetitions, not one
  call.
- **Not serializable.** A `Stopwatch` is process-local state; don't store it in
  a checkpoint.
- **One clock, one source.** Don't mix `std::chrono::system_clock` timestamps
  with `fp::now_seconds()` values — the epochs differ.
