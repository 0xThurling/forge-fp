#!/usr/bin/env bash
set -euo pipefail
# Build and run the real-SYCL smoke test with whichever SYCL toolchain is
# installed. Prefers oneAPI DPC++ (icpx/dpcpp), then AdaptiveCpp (acpp).
#
# Device preference is compile-time: FP_GPU_DEVICE_CPU / FP_GPU_DEVICE_ACCELERATOR
# can be added to the flags to override the default GPU-first selection.
mkdir -p build

FLAGS=(-std=c++20 -O2 -Isrc)

if command -v icpx >/dev/null 2>&1; then
  echo "== oneAPI DPC++ (icpx) =="
  icpx "${FLAGS[@]}" -fsycl bench/gpu_smoke.cpp -o build/gpu_smoke
elif command -v dpcpp >/dev/null 2>&1; then
  echo "== oneAPI DPC++ (dpcpp) =="
  dpcpp "${FLAGS[@]}" bench/gpu_smoke.cpp -o build/gpu_smoke
elif command -v acpp >/dev/null 2>&1; then
  echo "== AdaptiveCpp (acpp) =="
  acpp "${FLAGS[@]}" --acpp-targets=generic bench/gpu_smoke.cpp -o build/gpu_smoke
else
  echo "no SYCL toolchain found."
  echo "install oneAPI DPC++ (icpx) or AdaptiveCpp (acpp), then re-run."
  echo "for CPU-only correctness without a toolchain, use:"
  echo "  scripts/run_gpu_stub_test.sh"
  exit 0
fi

./build/gpu_smoke
