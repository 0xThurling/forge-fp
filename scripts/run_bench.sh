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
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/string_bench.cpp -o build/string_bench
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/random_bench.cpp -o build/random_bench
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/serialize_bench.cpp -o build/serialize_bench
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/ranges_bench.cpp -o build/ranges_bench
g++ -std=c++20 -O2 -march=native -Isrc -Ibench \
    bench/io_bench.cpp -o build/io_bench

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
echo "== string =="
"${PIN[@]}" ./build/string_bench
echo "== random =="
"${PIN[@]}" ./build/random_bench
echo "== serialize =="
"${PIN[@]}" ./build/serialize_bench
echo "== ranges/views =="
"${PIN[@]}" ./build/ranges_bench
echo "== io =="
"${PIN[@]}" ./build/io_bench