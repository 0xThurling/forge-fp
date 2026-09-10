# testing.md — tests, examples, CI

Everything in the other files is *unverifiable* until this exists. This is
the highest-leverage file in the folder.

## 1. Test harness — Forge-native GoogleTest, not a hand-rolled one [P0]

**Why:** The library has **zero tests** despite 15 headers and a README that
documents dozens of behaviors ("fails fast on the first error", "short-
circuits on missing value", "dedupes keeps consecutive only"). A
combinator library's contract is its semantics; every reorder of
`std::move`, every `if constexpr` branch, is a latent bug that only tests
catch. `forge.lua` currently says `testing = false`; flip it.

**How Forge's testing actually works** (from `forge ref`,
`CMakeGeneration/Sections/TestingSection.cs` + `Commands/TestCommand.cs`):

- The Testing CMake section is enabled **only when BOTH** conditions hold:
  `testing = true` in `forge.lua` **and** a dependency literally named
  `googletest` in `dependencies.direct`. Missing either → no test target is
  generated at all.
- `forge build` auto-creates a `test/` directory (with a sample test) when
  `testing = true`.
- The generated `.config/cmake/CMakeLists.txt` globs **`test/*.cpp`**
  (recursively — `GLOB_RECURSE`), builds a `forgefp_tests` executable
  linked against `GTest::gtest_main`, gives it `src/` + `include/` include
  dirs, and registers every test with `gtest_discover_tests` (→ `ctest`).
- Because ForgeFP is header-only, `APP_SOURCES` (src/*.cpp) contributes
  nothing; the test exe is pure test code.
- There is **no custom test harness to write**: use GoogleTest macros and
  let `gtest_main` supply `main()`.

**forge.lua changes** — testing requires the googletest git dependency:

```lua
return {
  project = {
    name = "ForgeFP",
    type = "library",
    standard = "20",
    install_headers = true
  },
  testing = true,
  dependencies = {
    direct = {
      googletest = {
        git = "https://github.com/google/googletest.git",
        tag = "v1.14.0"
      }
    },
    conan = {}
  },
  resources = { files = {} },
  scripts = {},
  features = {}
}
```

Then `forge build` regenerates `CMakeLists.txt` + `.config/cmake/CMakeLists.txt`
(the generated file says "DO NOT TOUCH" — set test policy in `forge.lua`, e.g.
extra flags via `forge.add_cmake([[...]])`, see meta.md §8).

**Running tests:**

```bash
forge build            # regenerates cmake + builds, fetches googletest via FetchContent
forge test             # builds + runs build/run_tests (suite name optional)
forge test --filter="Vec.*"     # gtest filter
ctest --test-dir build --output-on-failure   # gtest_discover_tests paths
```

**Skeleton** — one file per module in `test/`:

```cpp
// test/vec.test.cpp
#include <fp/vec.hpp>
#include <gtest/gtest.h>

TEST(VecTake, ClampsAtBounds) {
  std::vector<int> v{1, 2, 3};
  EXPECT_EQ(fp::take(v, 2), (std::vector<int>{1, 2}));
  EXPECT_TRUE(fp::drop(v, 10).empty());
}

TEST(VecChunk, ZeroIsEmpty) {
  std::vector<int> v{1, 2};
  EXPECT_TRUE(fp::chunk(v, 0).empty());
}
```

Note: use `(std::vector<int>{...})` parens inside `EXPECT_EQ` to dodge the
most-vexing-parse. `gtest` headers come from the FetchContent `_deps` include
path that Forge adds automatically.

## 2. Coverage matrix — what each module's tests must assert [P0]

**Implementation** — a worked example following the §1 forge-native skeleton:

```cpp
// test/vec.test.cpp
#include <fp/vec.hpp>
#include <gtest/gtest.h>

TEST(VecTake, ClampsAtBounds) {
  std::vector<int> v{1, 2, 3};
  EXPECT_EQ(fp::take(v, 2), (std::vector<int>{1, 2}));
  EXPECT_TRUE(fp::drop(v, 10).empty());
}

TEST(VecChunk, ZeroIsEmpty) {
  std::vector<int> v{1, 2};
  EXPECT_TRUE(fp::chunk(v, 0).empty());
}
```

| Module | Minimal assertions (in-order of value) |
|---|---|
| `maybe` | map on empty short-circuits; and_then fails through; or_else fallback & lazy overload; collect fail-fast; filter keeps/drops; flatten. |
| `either` | ok/err state transitions; map preserves err; and_then short-circuits (f not called); map_error rewrites; flatten; lefts/rights split; to_optional. |
| `result` | sequence first-error-return; from_optional both branches; traverse stops at first error; combine2 both-success & short-circuit. |
| `validation` | check both branches; validate_all accumulates *all* messages; combine2 accumulates; traverse over vector reports every failure; to_result collapse. |
| `vec` | take/drop boundaries (n=0, n>size); chunk(0) → empty; partition stability; group_by keys; zip truncation; flat_map concat; enumerate indices. |
| `ranges` | map/filter over views (not just vector); fold_left init type; take/drop over non-sized ranges (infinite view!); concat of views; to_vector. |
| `string` | trim only ' ' by default + explicit '\t'; split drops the trailing empty segment (`"a,b,"` → `{"a","b"}`, decided — matches the `char` overload via `getline`; the `string`-delimiter overload must skip the final empty push); join empty; replace_all with "" guard; strip_prefix no-op. |
| `compose`/`curry` | compose right-to-left; pipe left-to-right; currying accumulates then calls; zero-arg & default-arg functions (is_invocable decision); uncurry round-trip. |
| `memoize` | result cached (spy counting calls); distinct args distinct results; recursive fib parses. |
| `concurrent` | par_map order preserved for large vectors; empty/single-thread fallback; actor Send order; actor Ask future resolves to post-handler state; Channel blocking pairs (send/recv in threads + timeout). |
| `simd` | map_inplace full chunk + tail (odd sizes: 1, width-1, width+1); reduce vs scalar reference; dot vs scalar; map_to type change; masked tail equals scalar tail. |

**Why:** This table is the *definition of done* for the other files: a
feature listed in `vec.md` isn't "implemented" until its row's assertion
passes. Copy the table into the module file when implementing.

## 3. Compile-time tests — `static_assert` suites [P1]

```
test/compile/  — type-level assertions (compiled into the gtest exe; zero runtime):
  vec::map<int, invert> result_type; ranges::filter on const& view; curry arity;
  either::and_then return type; validation::combine2 error accumulation type
```

**Why:** Half of this library's API is *types* (template signatures,
`invoke_result_t` plumbing). Runtime tests can't catch an accidental
`vector<const T>` or a broken CTAD. A compile-only suite is nearly free
(no runtime, no leaks) and locks the public surface against signature
drift between `src/fp` and `include/forgefp/fp` — the two trees must
compile the *same* TU. Files with only `static_assert`s can live in
`test/compile/` (forge's recursive `test/*.cpp` glob picks them up
automatically) or be syntax-checked separately in CI.

## 4. Tree-drift guard — src/include must stay byte-identical [P0]

```
scripts/check_header_sync.sh  →  diff -rq src/fp include/forgefp/fp; exit 1 on drift
```

**Why:** Every feature in this folder lands in *two* trees by hand. The drift
is not hypothetical — `ranges.h`/`ranges.hpp` rename and the "older subset"
README claim show it already happened. A five-line diff check turns a
silent maintenance hazard into a loud one. Cheap, mechanical, and protects
every other change. Wire it into forge so it's one command away:

```lua
-- forge.lua
scripts = {
  ["check-sync"] = "bash scripts/check_header_sync.sh",
},
```

```bash
forge run check-sync   # or run the script directly in CI
```

## 5. Examples — compile-and-run docs [P1]

```
examples/          # one .cpp per module, kept buildable by CI
  examples/vec.cpp, examples/compose.cpp, examples/result.cpp, ...
```

**Why:** The README is 476 lines of prose with code snippets that *nothing
executes* — the "Examples and bugfixes" commit (the commit that fixed
examples in the README) proves snippets rot. Real `.cpp` files that CI
compiles turn the README's contract into a checked artifact, and give users
a starting point that actually links (include paths, `-pthread`, simd
include). Run them through a forge script for a one-command smoke test:

```lua
-- forge.lua
scripts = {
  ["examples"] = "for f in examples/*.cpp; do "
                 .. "g++ -std=c++20 -Isrc \"$f\" -pthread -o build/example && "
                 .. "./build/example || exit 1; done",
},
```

```bash
forge run examples
```

P1.

## 6. CI matrix [P1]

**Implementation** — reference workflow (structure only; adapt triggers/names).
`on: [push, pull_request]`; tests run via ctest (gtest_discover_tests)

```yaml
# .github/workflows/ci.yml
name: CI
on: [push, pull_request]

jobs:
  build-and-test:
    strategy:
      matrix:
        compiler: [gcc-12, gcc-13, clang-17]
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Configure (fetches googletest via FetchContent)
        run: cmake -S . -B build -DCMAKE_CXX_COMPILER=${{ matrix.compiler }}
      - name: Build
        run: cmake --build build

      - name: Header hygiene (each header compiles standalone)
        run: |
          for h in src/fp/*.hpp; do
            ${{ matrix.compiler }} -std=c++20 -fsyntax-only -Isrc "$h"
          done

      - name: Header-tree sync (src vs include)
        run: diff -rq src/fp include/forgefp/fp

      - name: Run tests
        run: ctest --test-dir build --output-on-failure

  compile-time-tests:          # static_assert suites, no runtime needed
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: |
          g++ -std=c++20 -fsyntax-only -Isrc test/compile/*.cpp
          clang++ -std=c++20 -fsyntax-only -Isrc test/compile/*.cpp
```

Add a sanitizer job (`-fsanitize=address,undefined`) once concurrency tests
exist — see §7.

```
.github/workflows/ci.yml
  jobs: build-and-test
    matrix: gcc-{12,13,14}, clang-{17,18}, std: 20
    steps: cmake configure → build → run tests → run examples
    plus: compile-time tests, header-sync, README snippet smoke (examples/)
```

**Why:** GCC 11+/Clang 14+ is the README's supported matrix and *no machine
runs it*. Header-only libraries fail in one-flag configs (simd path,
`-pthread` presence, libstdc++ vs libc++ `<experimental/simd>`). A CI that
exercises the matrix is the only thing that makes the README's Requirements
table honest. P1 — this is what turns "claimed" into "tested".

## 7. Sanitizers on the concurrency tests [P2]

**Implementation** — inject flags via Forge's custom-CMake hook in `forge.lua`
(keep the generated `.config/cmake/CMakeLists.txt` untouched):

```lua
-- forge.lua (only while running sanitized tests)
forge.add_cmake([[
  target_compile_options(forgefp_tests PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(forgefp_tests PUBLIC -fsanitize=address,undefined)
]])
```

Or, purely in CI, pass flags through the generated CMake via an `ENV`:

```bash
CXXFLAGS="-fsanitize=address,undefined" cmake -S . -B build-san
cmake --build build-san
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-san --output-on-failure
```

**Why:** The actor's mutex/queue/cv and the future plumbing are where
lifetime bugs live (the `[f, x]`-capture-by-reference hazard from
`concurrent.md` is undetectable except under ASan). Adding sanitizer runs
to the test job costs one profile and catches the nastiest class of bug in
this library. P2 — after (6) exists.

## 8. Benchmarks for simd & concurrency [P2]

**Implementation** — full worked harness, per-function targets, forge wiring
(`forge run bench`), and Google-Benchmark upgrade path in
**[`bench.md`](bench.md)**.

**Why:** SIMD/concurrency code that isn't benchmarked is code with an unproven
reason to exist; the modules' *entire value* is speed. The `bench.md` harness
(`bench/bench.hpp`, zero dependencies) validates the README's
"SIMD-accelerated" / "parallel map" claims and prevents accidental
regressions (e.g. alignment pessimization, chunking regressions). P2 — after
(6).
