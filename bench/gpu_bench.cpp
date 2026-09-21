// fp::gpu vs the CPU paths — the crossover benchmark.
//
// For each kernel fp/gpu.hpp exposes, measure the fastest CPU path
// (fp::inplace / fp::simd / fp::linalg / fp::numerics) and the GPU path across
// sizes, then report the smallest size at which the GPU wins. The GPU tables
// also show the phase 2 variants where they exist: `reduce`/`dot` with a
// reusable Scratch, `softmax_rows` with the work-group kernel.
//
// GPU timings are synchronous: every fp::gpu call ends in `wait_and_throw()`,
// so they include launch + wait + execution.
//
// Build with a real SYCL toolchain:
//   scripts/run_gpu_build.sh --bench
// Build without SYCL (fallback sanity check; GPU rows are skipped):
//   g++ -std=c++20 -O2 -march=native -Isrc -Ibench bench/gpu_bench.cpp -pthread
#include <fp/gpu.hpp>

#include <fp/inplace.hpp>
#include <fp/numerics.hpp>
#include <fp/ranges.hpp>
#include <fp/simd.hpp>

#include "bench.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <span>
#include <vector>

namespace {

constexpr std::size_t sizes[] = {1u << 10, 1u << 14, 1u << 18, 1u << 22};
constexpr std::size_t softmax_cols = 256;
constexpr double target_ns = 30'000'000.0; // per calibrated sample
constexpr int samples = 3;

// Warm up (this is also where AdaptiveCpp JIT-compiles a kernel: 100-300 ms,
// cached across runs), calibrate repetitions so one sample takes at least
// `target_ns`, and keep the best of `samples` — bench::measure's estimator
// without its printing.
template <class F> double time_ns(F &&f) {
  f();
  double best = 0.0;
  for (int s = 0; s < samples; ++s) {
    std::size_t reps = 1;
    double t = 0.0;
    while ((t = bench::time_rep(f, reps)) * reps < target_ns &&
           reps < (1u << 26))
      reps *= 2;
    best = best == 0.0 ? t : std::min(best, t);
  }
  return best;
}

struct Summary {
  char const *kernel;
  std::size_t crossover; // smallest n where the GPU won; 0 = never in range
};

std::vector<Summary> summaries;
std::size_t crossover = 0;

void note_crossover(std::size_t n, double cpu_best, double gpu_best) {
  if (gpu_best > 0.0 && gpu_best < cpu_best && crossover == 0)
    crossover = n;
}

void end(char const *kernel) { summaries.push_back({kernel, crossover}); }

// --- 3-column tables: elements | cpu A | cpu B | gpu -------------------------

void begin(char const *title, char const *cpu_a, char const *cpu_b) {
  crossover = 0;
  std::printf("\n== %s ==\n", title);
  std::printf("%12s %13s %13s %13s %10s %8s\n", "elements", cpu_a, cpu_b, "gpu",
              "gpu GB/s", "faster");
}

void row(std::size_t n, double cpu_a, double cpu_b, double gpu) {
  char gpu_text[32] = "-";
  char bw_text[32] = "-";
  const double cpu_best = std::min(cpu_a, cpu_b);
  char const *winner = "cpu";
  if (gpu > 0.0) {
    std::snprintf(gpu_text, sizeof gpu_text, "%.1f", gpu);
    std::snprintf(bw_text, sizeof bw_text, "%.2f",
                  double(n) * double(sizeof(float)) / gpu);
    if (gpu < cpu_best) {
      winner = "gpu";
      note_crossover(n, cpu_best, gpu);
    }
  }
  std::printf("%12zu %13.1f %13.1f %13s %10s %8s\n", n, cpu_a, cpu_b, gpu_text,
              bw_text, winner);
}

// --- 4-column tables: a second GPU variant (scratch, work-group, …) ----------

void begin4(char const *title, char const *cpu_a, char const *cpu_b,
            char const *gpu_a, char const *gpu_b) {
  crossover = 0;
  std::printf("\n== %s ==\n", title);
  std::printf("%12s %13s %13s %13s %13s %10s %8s\n", "elements", cpu_a, cpu_b,
              gpu_a, gpu_b, "gpu GB/s", "faster");
}

void row4(std::size_t n, double cpu_a, double cpu_b, double gpu_a,
          double gpu_b) {
  char a_text[32] = "-", b_text[32] = "-", bw_text[32] = "-";
  const double cpu_best = std::min(cpu_a, cpu_b);
  double gpu_best = 0.0;
  if (gpu_a > 0.0)
    gpu_best = gpu_a;
  if (gpu_b > 0.0)
    gpu_best = gpu_best == 0.0 ? gpu_b : std::min(gpu_best, gpu_b);
  if (gpu_a > 0.0)
    std::snprintf(a_text, sizeof a_text, "%.1f", gpu_a);
  if (gpu_b > 0.0)
    std::snprintf(b_text, sizeof b_text, "%.1f", gpu_b);
  char const *winner = "cpu";
  if (gpu_best > 0.0) {
    std::snprintf(bw_text, sizeof bw_text, "%.2f",
                  double(n) * double(sizeof(float)) / gpu_best);
    if (gpu_best < cpu_best) {
      winner = "gpu";
      note_crossover(n, cpu_best, gpu_best);
    }
  }
  std::printf("%12zu %13.1f %13.1f %13s %13s %10s %8s\n", n, cpu_a, cpu_b,
              a_text, b_text, bw_text, winner);
}

// ---------------------------------------------------------------------------

void transform_inplace_bench() {
  begin("transform_inplace: x <- 0.5x + 0.25", "cpu:inplace", "cpu:simd");
  const auto f = [](float x) { return 0.5f * x + 0.25f; };
  for (std::size_t n : sizes) {
    std::vector<float> host(n, 1.0f);
    std::span<float> span{host};

    const double cpu_a = time_ns([&] { fp::transform_inplace(span, f); });
    const double cpu_b = time_ns([&] {
      fp::map_inplace(host, [](fp::vec<float> x) { return x * 0.5f + 0.25f; });
    });

    double gpu = 0.0;
    if (fp::gpu::usable()) {
      auto buf = fp::gpu::Buffer<float>::from_host(host);
      if (buf.is_ok())
        gpu = time_ns([&] { fp::gpu::transform_inplace(buf.value(), f); });
    }
    row(n, cpu_a, cpu_b, gpu);
  }
  end("transform_inplace");
}

void map_to_bench() {
  begin("map_to: dst <- 1.0001 * src", "cpu:span", "cpu:simd(alloc)");
  const auto f = [](float x) { return 1.0001f * x; };
  for (std::size_t n : sizes) {
    std::vector<float> src(n, 1.0f), dst(n, 0.0f);
    std::span<float const> s{src};
    std::span<float> d{dst};

    const double cpu_a = time_ns([&] { fp::map_to(s, d, f); });
    const double cpu_b = time_ns([&] {
      auto out = fp::map_to(src, [](fp::vec<float> x) { return x * 1.0001f; });
      bench::keep(&out);
    });

    double gpu = 0.0;
    if (fp::gpu::usable()) {
      auto src_dev = fp::gpu::Buffer<float>::from_host(src);
      auto dst_dev = fp::gpu::Buffer<float>::alloc(n);
      if (src_dev.is_ok() && dst_dev.is_ok())
        gpu = time_ns(
            [&] { fp::gpu::map_to(dst_dev.value(), src_dev.value(), f); });
    }
    row(n, cpu_a, cpu_b, gpu);
  }
  end("map_to");
}

void reduce_bench() {
  begin4("reduce: sum", "cpu:fold_left", "cpu:simd", "gpu:alloc", "gpu:scratch");
  for (std::size_t n : sizes) {
    std::vector<float> host(n, 1.0f);

    const double cpu_a = time_ns([&] {
      auto r = fp::fold_left(std::span<float const>{host}, 0.0f, fp::plus);
      bench::keep(&r);
    });
    const double cpu_b = time_ns([&] {
      auto r = fp::reduce(host);
      bench::keep(&r);
    });

    double gpu_a = 0.0, gpu_b = 0.0;
    if (fp::gpu::usable()) {
      auto buf = fp::gpu::Buffer<float>::from_host(host);
      fp::gpu::Scratch<float> scratch;
      if (buf.is_ok()) {
        gpu_a = time_ns([&] {
          auto r = fp::gpu::reduce(buf.value(), 0.0f);
          bench::keep(&r);
        });
        gpu_b = time_ns([&] {
          auto r = fp::gpu::reduce(buf.value(), 0.0f, scratch);
          bench::keep(&r);
        });
      }
    }
    row4(n, cpu_a, cpu_b, gpu_a, gpu_b);
  }
  end("reduce");
}

void dot_bench() {
  begin4("dot: <a, b>", "cpu:linalg", "cpu:simd", "gpu:alloc", "gpu:scratch");
  for (std::size_t n : sizes) {
    std::vector<float> a(n, 1.0f), b(n, 2.0f);
    std::span<float const> sa{a}, sb{b};

    const double cpu_a = time_ns([&] {
      auto r = fp::dot<float>(sa, sb);
      bench::keep(&r);
    });
    const double cpu_b = time_ns([&] {
      auto r = fp::dot<float>(a, b);
      bench::keep(&r);
    });

    double gpu_a = 0.0, gpu_b = 0.0;
    if (fp::gpu::usable()) {
      auto da = fp::gpu::Buffer<float>::from_host(a);
      auto db = fp::gpu::Buffer<float>::from_host(b);
      fp::gpu::Scratch<float> scratch;
      if (da.is_ok() && db.is_ok()) {
        gpu_a = time_ns([&] {
          auto r = fp::gpu::dot(da.value(), db.value());
          bench::keep(&r);
        });
        gpu_b = time_ns([&] {
          auto r = fp::gpu::dot(da.value(), db.value(), scratch);
          bench::keep(&r);
        });
      }
    }
    row4(n, cpu_a, cpu_b, gpu_a, gpu_b);
  }
  end("dot");
}

void axpy_bench() {
  begin("axpy: y <- y + a*x (a = -1e-6)", "cpu:zip", "cpu:simd");
  constexpr float a = -1e-6f;
  for (std::size_t n : sizes) {
    std::vector<float> x(n, 1.0f), y(n, 1.0f);
    std::span<float> sy{y};
    std::span<float const> sx{x};

    const double cpu_a = time_ns([&] {
      fp::zip_transform_inplace(
          sy, sx, [](float yi, float xi) { return yi + a * xi; });
    });
    const double cpu_b = time_ns([&] { fp::axpy_inplace(y, a, x); });

    double gpu = 0.0;
    if (fp::gpu::usable()) {
      auto dy = fp::gpu::Buffer<float>::from_host(y);
      auto dx = fp::gpu::Buffer<float>::from_host(x);
      if (dy.is_ok() && dx.is_ok())
        gpu = time_ns([&] { fp::gpu::axpy_inplace(dy.value(), a, dx.value()); });
    }
    row(n, cpu_a, cpu_b, gpu);
  }
  end("axpy");
}

void softmax_bench() {
  begin4("softmax_rows (256 cols)", "cpu:rows", "cpu:softmax(alloc)",
         "gpu:row", "gpu:work-group");
  for (std::size_t n : sizes) {
    const std::size_t rows = n / softmax_cols;
    std::vector<std::vector<float>> grid(rows,
                                         std::vector<float>(softmax_cols, 1.0f));
    std::vector<float> flat(n, 1.0f);

    const double cpu_a = time_ns([&] { fp::softmax_rows(grid); });
    const double cpu_b = time_ns([&] {
      std::vector<std::vector<double>> out;
      out.reserve(grid.size());
      for (auto const &r : grid)
        out.push_back(fp::softmax(r));
      bench::keep(&out);
    });

    double gpu_a = 0.0, gpu_b = 0.0;
    if (fp::gpu::usable()) {
      auto buf = fp::gpu::Buffer<float>::from_host(flat);
      auto buf_wg = fp::gpu::Buffer<float>::from_host(flat);
      if (buf.is_ok())
        gpu_a = time_ns(
            [&] { fp::gpu::softmax_rows(buf.value(), rows, softmax_cols); });
      if (buf_wg.is_ok())
        gpu_b = time_ns([&] {
          fp::gpu::softmax_rows_wg(buf_wg.value(), rows, softmax_cols);
        });
    }
    row4(n, cpu_a, cpu_b, gpu_a, gpu_b);
  }
  end("softmax_rows");
}

void row_kernels_bench() {
  begin("row_sums (256 cols)", "cpu:linalg", "cpu:fold");
  for (std::size_t n : sizes) {
    const std::size_t rows = n / softmax_cols;
    std::vector<std::vector<float>> grid(rows,
                                         std::vector<float>(softmax_cols, 1.0f));
    std::vector<float> flat(n, 1.0f);

    const double cpu_a = time_ns([&] {
      auto r = fp::row_sums(grid);
      bench::keep(&r);
    });
    const double cpu_b = time_ns([&] {
      float total = 0.0f;
      for (auto const &r : grid)
        total += fp::fold_left(r, 0.0f, fp::plus);
      bench::keep(&total);
    });

    double gpu = 0.0;
    if (fp::gpu::usable()) {
      auto buf = fp::gpu::Buffer<float>::from_host(flat);
      if (buf.is_ok())
        gpu = time_ns([&] {
          auto r = fp::gpu::row_sums(buf.value(), rows, softmax_cols);
          bench::keep(&r);
        });
    }
    row(n, cpu_a, cpu_b, gpu);
  }
  end("row_sums");
}

void matmul_bench() {
  constexpr std::size_t dims[] = {128, 256, 512, 1024};
  std::printf("\n== matmul (row-major N x N x N, float) ==\n");
  std::printf("%12s %16s %14s %12s %12s %8s\n", "N", "cpu:fp::matmul",
              "gpu", "cpu GFLOP/s", "gpu GFLOP/s", "faster");
  std::size_t crossover_n = 0;
  for (std::size_t N : dims) {
    const std::size_t n = N * N;
    std::vector<std::vector<float>> ga(N, std::vector<float>(N, 1.0f));
    std::vector<std::vector<float>> gb(N, std::vector<float>(N, 2.0f));
    std::vector<float> fa(n), fb(n);
    for (std::size_t i = 0; i < n; ++i) {
      fa[i] = 1.0f;
      fb[i] = 2.0f;
    }

    const double cpu = time_ns([&] {
      auto r = fp::matmul(ga, gb);
      bench::keep(&r);
    });

    double gpu = 0.0;
    if (fp::gpu::usable()) {
      auto da = fp::gpu::Buffer<float>::from_host(fa);
      auto db = fp::gpu::Buffer<float>::from_host(fb);
      if (da.is_ok() && db.is_ok())
        gpu = time_ns([&] {
          auto r = fp::gpu::matmul(da.value(), db.value(), N, N, N);
          bench::keep(&r);
        });
    }

    const double flops = 2.0 * double(N) * N * N;
    char gpu_text[32] = "-";
    char gpu_flops[32] = "-";
    char const *winner = "cpu";
    if (gpu > 0.0) {
      std::snprintf(gpu_text, sizeof gpu_text, "%.1f", gpu);
      std::snprintf(gpu_flops, sizeof gpu_flops, "%.1f",
                    flops / gpu); // FLOP / ns == GFLOP/s
      if (gpu < cpu) {
        winner = "gpu";
        if (crossover_n == 0)
          crossover_n = N;
      }
    }
    std::printf("%12zu %16.1f %14s %12.1f %12s %8s\n", N, cpu, gpu_text,
                flops / cpu, gpu_flops, winner);
  }
  summaries.push_back({"matmul", crossover_n});
}

void transfer_bench() {
  std::printf("\n== transfer (float; pageable vs reusable pinned staging) ==\n");
  std::printf("%12s %15s %20s %15s %15s\n", "bytes", "H2D pageable",
              "H2D pinned (copy_from)", "D2H pageable", "D2H pinned");
  for (std::size_t n : sizes) {
    const std::size_t bytes = n * sizeof(float);
    std::vector<float> host(n, 1.0f);

    char h2d[32] = "-", d2h[32] = "-", h2d_pin[32] = "-", d2h_pin[32] = "-";
    if (fp::gpu::usable()) {
      // Pageable: from_host allocates, to_host allocates the destination
      // vector, so both numbers include their allocation.
      auto buf = fp::gpu::Buffer<float>::from_host(host);
      if (buf.is_ok()) {
        const double t =
            time_ns([&] { (void)fp::gpu::Buffer<float>::from_host(host); });
        std::snprintf(h2d, sizeof h2d, "%.2f", double(bytes) / t);
        const double t2 = time_ns([&] {
          auto out = buf.value().to_host();
          bench::keep(&out);
        });
        std::snprintf(d2h, sizeof d2h, "%.2f", double(bytes) / t2);
      }

      // Pinned: one staging block, reused for every transfer (phase 1c).
      // `copy_from` is the intended H2D path — the producer writes into
      // staging, then a single DMA. `to_host(staging)` is the D2H path with no
      // allocation. Routing an existing std::vector *through* staging is not
      // measured because the extra host copy makes it slower than pageable.
      auto staging = fp::gpu::HostBuffer<float>::alloc(n);
      if (staging.is_ok()) {
        std::copy(host.begin(), host.end(), staging.value().span().begin());
        auto pinned_buf = fp::gpu::Buffer<float>::from_host(host);
        if (pinned_buf.is_ok()) {
          const double t =
              time_ns([&] { pinned_buf.value().copy_from(staging.value()); });
          std::snprintf(h2d_pin, sizeof h2d_pin, "%.2f", double(bytes) / t);
          const double t2 = time_ns(
              [&] { pinned_buf.value().to_host(staging.value()); });
          std::snprintf(d2h_pin, sizeof d2h_pin, "%.2f", double(bytes) / t2);
        }
      }
    }
    std::printf("%12zu %15s %20s %15s %15s\n", bytes, h2d, h2d_pin, d2h,
                d2h_pin);
  }
}

} // namespace

int main() {
  std::printf("fp::gpu crossover benchmark\n");
  if (fp::gpu::available) {
    const auto backend = fp::gpu::backend_info();
    if (backend.is_ok())
      std::printf("backend: %s | %s | %s | driver %s\n",
                  backend.value().implementation.c_str(),
                  backend.value().platform.c_str(),
                  backend.value().device.c_str(),
                  backend.value().driver_version.c_str());
    std::printf("usable (USM device allocations): %s\n",
                fp::gpu::usable() ? "yes" : "no");
  } else {
    std::printf("no SYCL toolchain in this build: GPU rows are skipped\n");
  }
  std::printf("sizes: 1K, 16K, 256K, 4M elements (float); best of %d samples, "
              ">= %.0f ms each\n",
              samples, target_ns / 1e6);

  transform_inplace_bench();
  map_to_bench();
  reduce_bench();
  dot_bench();
  axpy_bench();
  softmax_bench();
  row_kernels_bench();
  matmul_bench();
  transfer_bench();

  std::printf("\n== crossover summary (smallest size where the GPU beat the "
              "fastest CPU path) ==\n");
  for (auto const &s : summaries) {
    if (s.crossover != 0)
      std::printf("%-20s >= %zu\n", s.kernel, s.crossover);
    else
      std::printf("%-20s never (up to %zu elements / %zu)\n", s.kernel,
                  sizes[sizeof(sizes) / sizeof(sizes[0]) - 1],
                  std::size_t{1024});
  }
  return 0;
}
