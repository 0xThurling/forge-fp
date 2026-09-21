#!/usr/bin/env bash
set -euo pipefail
# Compile and run fp/gpu.hpp's SYCL branch against the CPU stub in
# test/support (no SYCL toolchain required). This verifies that the SYCL code
# path compiles, allocates/frees correctly, and produces the same values as
# the CPU fallback.
mkdir -p build
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Isrc -Itest/support \
    bench/gpu_stub_test.cpp -o build/gpu_stub_test
./build/gpu_stub_test
