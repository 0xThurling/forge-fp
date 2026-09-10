# meta.md — repo hygiene, docs sync, build config

Thread that ties the feature work together. None of this adds API surface;
all of it prevents the entire backlog from rotting.

## 1. Header-tree sync workflow [P0]

```
Rule: src/fp/<x> and include/forgefp/fp/<x> are BYTE-IDENTICAL at all times.
```

**Why:** CMake installs `include/` only (`.config/cmake/CMakeLists.txt` →
`target_include_directories(forgefp INTERFACE include)`); README users point
at `src/`. Two trees = two truths, and the project already drifted once
(`ranges.h`/`ranges.hpp` rename shipped in `src` only until the last commit).
Mechanize it:

```bash
# after any edit
cp src/fp/* include/forgefp/fp/          # blind sync
diff -rq src/fp include/forgefp/fp && echo SYNCED
```

**CI guard script** — `scripts/check_header_sync.sh` (fails loudly):

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if ! diff -rq src/fp include/forgefp/fp; then
  echo "DRIFT: src/fp and include/forgefp/fp are out of sync" >&2
  echo "fix with: cp src/fp/* include/forgefp/fp/" >&2
  exit 1
fi
echo "header trees in sync"
```

plus the CI guard from `testing.md` (§4).

## 2. README sync — claims that are currently false [P0]

| README claim | Reality | Fix |
|---|---|---|
| "`all.hpp` does **not** include ... `simd.hpp`" (Limitations & §14) | Already true (fixed earlier); keep it true — re-check every time simd changes. | Verify after every `all.hpp` edit. |
| `include <fp/ranges.h>` (Getting started, §7, §14) | File is `ranges.hpp` (renamed in last commit, not yet in README). | s/ranges.h/ranges.hpp across README. |
| "`include/forgefp/fp/` — the installed/public subset (`adt`, `either`, `maybe`, `result`)" (§Getting, §Limitations) | Full copy (14 headers) except it once lacked ranges — now synced. | Rewrite to describe it as the installed copy of the full API; stop calling it an "older subset". |
| "`all.hpp` ... everything except ranges" (README §7 note) | True today; re-check every include added to `all.hpp`. | Keep the note honest. |
| "The library is header-only, no compilation step" (Requirements) | True — requires `src/fp` include path for `ranges.hpp`/`simd.hpp` users; document that CMake consumers get the include/ tree only. | Clarify one include path per consumption mode. |

**Why:** The README is the contract; every feature file in this folder adds
to it. A stale README makes each new feature *double* work (implement +
misdocument). Fixing the table above first means new features land in docs
that are already truthful.

## 3. `all.hpp` composition policy [P0]

```
Decide and document: all.hpp = stable, portable core (no simd, no ranges?).
```

**Why:** `simd.hpp` was removed from `all.hpp` to keep the umbrella portable
(`<experimental/simd>` fails on Clang+libc++/MSVC). The same logic argues
for `ranges.hpp` (it drags `<ranges>` + `<concepts>` and is the newest
module). Whatever the call: put it in `all.hpp` in ONE direction only, and
say so in §7/§14/Getting-started. Every new module (e.g. `channel.hpp`) must
ask this question before it ships.

## 4. Include hygiene per header [P1]

```
Check: every header compiles STANDALONE (-fsyntax-only on its own).
```

**Why:** `g++ -std=c++20 -fsyntax-only src/fp/x.hpp` catches missing
includes; all 15 currently pass on GCC 16. New headers (from this folder's
plan) must too. Add the loop to CI (`testing.md` §6) so hygiene is
enforced, not remembered.

## 5. Naming conventions [P1]

```
PascalCase members: actor::Send (stays), ThreadPool::enqueue (lower),
Channel::send/recv (lower). Free functions: snake_case. Error-side helpers:
map_error, or_else, and_then (lowercase, matching map/filter).
```

**Why:** The library mixes `actor.Send` (Pascal) with `fp::map` (snake);
free functions should stay snake_case consistently so pipe-style chains read
uniformly. Decide once for `Channel`, `ThreadPool`, `Async` and put it in
the README's style note — mixed conventions in *new* concurrency code are
where reviewer friction lives.

## 6. Compiler-version guards [P1]

```
#ifdef __cpp_lib_experimental_simd / __has_include(<experimental/simd>)
#  define FP_HAS_SIMD
#endif
```

**Why:** README requires GCC 11+ or Clang 14+ (libstdc++ recommended) — but
`<experimental/simd>` is genuinely absent on some Clang/libc++ combos and
MSVC. The current code hard-includes it; a `FP_HAS_SIMD` guard (in addition
to excluding simd from `all.hpp`) makes the portability claim *true* instead
of aspirational. All headers use `#error` consistently: `simd.hpp` fails
loudly with a clear message when `FP_HAS_SIMD` is undefined (a header that
silently degraded to scalar code would lie about performance), while
`all.hpp` simply skips including simd — the ignore happens at the aggregate
level, the error at the point of direct use.

## 7. Stale files / leftover scaffolding [P1]

```
assets/, external/        — empty dirs (already removed? verify)
.gitignore                — currently: build/, compile_commands.json
                            (lib/, conanfile.txt already pruned)
include/forgefp/maybe.hpp — stray duplicate (deleted in earlier commit; verify gone)
```

**Why:** Empty dirs and stray duplicates silently confuse consumers (two
include paths for one header = ODR confusion). Sweep before feature work so
the diff surface stays clean. Mostly done; verify with `git status` and
`find . -name '*.h' | grep -v src` before the next feature lands.

## 8. Build system — how Forge actually drives this project [P1]

Forge facts from `~/forge/ref` (authoritative for this repo):

- `forge build` regenerates two files: the root `CMakeLists.txt` (thin: sets
  `cmake_minimum_required` + `CMAKE_POLICY_VERSION_MINIMUM` + includes
  `.config/cmake/CMakeLists.txt`) and `.config/cmake/CMakeLists.txt`, which is
  composed of ordered sections (`standard`, `fetchcontent`, `conan`,
  `project_target`, `linking`, `testing`, `custom`). Both are generated —
  hand-edits are overwritten.
- **Library targets**: `ProjectTargetSection` globs `src/*.cpp`; if none
  exist it emits a header-only **INTERFACE** library. With
  `install_headers = true` (currently set) it adds
  `target_include_directories(forgefp INTERFACE include)` and
  `install(DIRECTORY include/ DESTINATION include)` — **only `include/`
  ships to consumers, never `src/`**. This is exactly why the §1 sync rule
  (src ↔ include byte-identical) is load-bearing.
- **Testing** is the only test integration Forge has, and it is GoogleTest:
  the testing section activates **only when** `testing = true` AND a
  dependency literally named `googletest` exists in `dependencies.direct`
  (see testing.md §1 for the exact `forge.lua`).
- **Dependencies**: git deps via `FetchContent` (`git` + `tag`), Conan
  packages via `find_package`; both are configured in `forge.lua`, not in
  CMake. googletest is therefore a normal git dependency, not a special
  setup step.
- **Scripts**: `scripts = { ["name"] = "shell cmd" }` run via
  `forge run name`; `pre-build`/`post-build` are automatic hooks. Useful for
  the header-sync guard and example smoke tests (testing.md §4/§5).
- **Custom CMake**: `forge.add_cmake([[...]])` in `forge.lua` injects
  arbitrary CMake (e.g. sanitizer flags, testing.md §7) without touching the
  generated files.
- **LSP**: Forge symlinks `compile_commands.json` into the project root
  automatically (already present, gitignored).
- **CMake policy**: Forge auto-applies `CMAKE_POLICY_VERSION_MINIMUM 3.5`
  on CMake 4+; override with `project.cmake_policy_version`.

Target forge.lua state after testing lands (see testing.md §1 for the full
file):

```lua
testing = true,
dependencies = {
  direct = {
    googletest = { git = "https://github.com/google/googletest.git",
                   tag = "v1.14.0" }
  },
  conan = {},
},
scripts = {
  ["check-sync"] = "bash scripts/check_header_sync.sh",
  ["examples"]   = "for f in examples/*.cpp; do g++ -std=c++20 -Isrc \"$f\" -pthread -o build/example && ./build/example || exit 1; done",
},
```

**Why:** `.config/cmake/CMakeLists.txt` installs `include/` wholesale, so new
headers flow to consumers automatically — but only if they land in BOTH
trees. The `forge build` regen loop is the source of truth; keep
`.config/cmake` untouched (generated header warns). Write all unit-test
policy through `forge.lua` (googletest dep + `testing` flag) and
`forge.add_cmake`, never by patching generated CMake.

## 9. Versioning / ABI notes [P2]

```
No version file, no CHANGELOG, no tags. header-only → no ABI, but API churn
matters (README examples are API).
```

**Why:** Every feature in this folder changes the public API. A
`CHANGELOG.md` with one line per feature (or at least a documented tag per
release) turns "I added stuff" commits into something consumers can
upgrade against. P2 — but start it with the first feature landing.

## 10. Definition of done (use as the checklist per feature) [P0]

```
For each feature in *.md files:
  [ ] implemented in src/fp/<module>.hpp
  [ ] byte-copied to include/forgefp/fp/<module>.hpp
  [ ] diff -rq clean
  [ ] standalone-compiles (-fsyntax-only) on g++ AND clang++ (if available)
  [ ] test added (testing.md §2 table row)
  [ ] README section updated (or section added)
  [ ] CHANGELOG line (once §9 exists)
```

**Why:** "Extensive list of missing features" only pays off if each item has
an exit criteria. This ten-point list is the exit criteria; paste it into
the top of every file when starting work.