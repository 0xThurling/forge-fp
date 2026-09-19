return {
  project = {
    name = "ForgeFP",
    type = "library",
    standard = "20",
    install_headers = true
  },
  build = {
    -- Named flag bundles: warnings + threads (override per-run with
    -- `forge build --preset simd` etc.).
    presets = { "warnings", "concurrency" }
  },
  testing = true,
  dependencies = {
   direct = {
     ["googletest"] = {
       git = "https://github.com/google/googletest.git",
       tag = "v1.14.0"
     }
   },
   conan = {}
  },
  resources = {
    files = {}
  },
  scripts = {
    ["bench"] = "bash scripts/run_bench.sh",
    ["bench-pinned"] = "bash scripts/run_bench.sh 2"
  },
  features = {}
}
