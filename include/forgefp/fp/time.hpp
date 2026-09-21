#pragma once
#include <chrono>

namespace fp {

// Monotonic seconds since an arbitrary epoch (steady_clock).
inline double now_seconds() {
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline double elapsed_seconds(double since) { return now_seconds() - since; }

// Per-step / per-frame timing. Monotonic only; no calendar or wall-clock code.
class Stopwatch {
public:
  Stopwatch() : start_(std::chrono::steady_clock::now()), lap_(start_) {}

  void reset() {
    start_ = std::chrono::steady_clock::now();
    lap_ = start_;
  }

  // Seconds since construction or the last reset().
  double elapsed() const { return seconds_since(start_); }

  // Seconds since the last lap() (or reset), then starts a new lap.
  double lap() {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - lap_).count();
    lap_ = now;
    return dt;
  }

private:
  static double seconds_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t)
        .count();
  }

  std::chrono::steady_clock::time_point start_;
  std::chrono::steady_clock::time_point lap_;
};

} // namespace fp
