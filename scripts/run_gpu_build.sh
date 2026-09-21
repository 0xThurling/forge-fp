#!/usr/bin/env bash
set -euo pipefail
# Build and run the real-SYCL smoke test with whichever SYCL toolchain is
# installed. Prefers oneAPI DPC++ (icpx/dpcpp), then AdaptiveCpp (acpp).
#
# Usage:
#   scripts/run_gpu_build.sh           # build + run bench/gpu_smoke.cpp
#   scripts/run_gpu_build.sh --bench   # also build + run bench/gpu_bench.cpp
#   scripts/run_gpu_build.sh --info    # print the toolchain/device report
#
# Device preference is compile-time: FP_GPU_DEVICE_CPU / FP_GPU_DEVICE_ACCELERATOR
# can be added to the flags to override the default GPU-first selection.
#
# AdaptiveCpp on WSL2 needs the toolchain prefix on PATH and the WSL driver
# shim (/usr/lib/wsl/lib, which holds libcuda.so.1) on the library path. Both
# are added below when they exist, so the script works from a clean shell.
mkdir -p build

if [ -d /opt/adaptivecpp/bin ]; then
  PATH="/opt/adaptivecpp/bin:$PATH"
fi
if [ -d /usr/lib/wsl/lib ]; then
  LD_LIBRARY_PATH="/usr/lib/wsl/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
export PATH LD_LIBRARY_PATH

FLAGS=(-std=c++20 -O2 -Isrc)
BENCH=0
for arg in "$@"; do
  case "$arg" in
    --bench) BENCH=1 ;;
    --info)
      if command -v acpp-info >/dev/null 2>&1; then
        acpp-info
      elif command -v sycl-ls >/dev/null 2>&1; then
        sycl-ls
      else
        echo "no SYCL info tool found (acpp-info / sycl-ls)"
      fi
      exit 0
      ;;
    *)
      echo "unknown option: $arg" >&2
      echo "usage: $0 [--bench] [--info]" >&2
      exit 2
      ;;
  esac
done

if command -v icpx >/dev/null 2>&1; then
  echo "== oneAPI DPC++ (icpx) =="
  CXX=icpx
  BENCH_FLAGS=("${FLAGS[@]}" -fsycl -Ibench -pthread)
  icpx "${FLAGS[@]}" -fsycl bench/gpu_smoke.cpp -o build/gpu_smoke
elif command -v dpcpp >/dev/null 2>&1; then
  echo "== oneAPI DPC++ (dpcpp) =="
  CXX=dpcpp
  BENCH_FLAGS=("${FLAGS[@]}" -fsycl -Ibench -pthread)
  dpcpp "${FLAGS[@]}" -fsycl bench/gpu_smoke.cpp -o build/gpu_smoke
elif command -v acpp >/dev/null 2>&1; then
  echo "== AdaptiveCpp (acpp) =="
  CXX=acpp
  # `generic` is the portable SSCP target: it avoids clang's CUDA frontend
  # (which breaks against CUDA 13.x headers) and JITs to the device at
  # runtime. Prefer it unless an explicit cuda:sm_XX target is required.
  BENCH_FLAGS=("${FLAGS[@]}" --acpp-targets=generic -Ibench -pthread)
  acpp "${FLAGS[@]}" --acpp-targets=generic bench/gpu_smoke.cpp -o build/gpu_smoke
else
  echo "no SYCL toolchain found."
  echo "install oneAPI DPC++ (icpx) or AdaptiveCpp (acpp), then re-run."
  echo "for CPU-only correctness without a toolchain, use:"
  echo "  scripts/run_gpu_stub_test.sh"
  exit 0
fi

./build/gpu_smoke

if [ "$BENCH" -eq 1 ]; then
  if [ ! -f bench/gpu_bench.cpp ]; then
    echo "bench/gpu_bench.cpp is not present yet (phase 1b); skipping --bench."
    exit 0
  fi
  echo
  echo "== bench/gpu_bench.cpp =="
  "$CXX" "${BENCH_FLAGS[@]}" bench/gpu_bench.cpp -o build/gpu_bench
  ./build/gpu_bench
fi
