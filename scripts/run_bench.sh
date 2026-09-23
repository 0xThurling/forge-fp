#!/usr/bin/env bash
set -euo pipefail
mkdir -p build
CPU="${1:-}"   # optional: cores to pin, e.g. "2" or "0-3"
PIN=()
[ -n "$CPU" ] && PIN=(taskset -c "$CPU")

g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/simd_bench.cpp -o build/simd_bench
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/concurrent_bench.cpp -pthread -o build/concurrent_bench
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/inplace_bench.cpp -o build/inplace_bench
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/numerics_bench.cpp -o build/numerics_bench
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/linalg_bench.cpp -o build/linalg_bench
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/bits_bench.cpp -o build/bits_bench
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/parse_bench.cpp -o build/parse_bench

echo "== simd =="
"${PIN[@]}" ./build/simd_bench
echo "== concurrent =="
"${PIN[@]}" ./build/concurrent_bench
echo "== inplace/memory =="
"${PIN[@]}" ./build/inplace_bench
echo "== numerics =="
"${PIN[@]}" ./build/numerics_bench
echo "== linalg =="
"${PIN[@]}" ./build/linalg_bench
echo "== bits =="
"${PIN[@]}" ./build/bits_bench
echo "== parse =="
"${PIN[@]}" ./build/parse_bench