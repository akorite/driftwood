# Continuous Integration

GitHub Actions workflow is configured in `.github/workflows/build.yml`
but is **not yet enabled** in this repository because the initial push
token did not have the `workflow` OAuth scope.

## One-time setup

You only need to do this once. From a checkout of the repository:

```bash
# 1. Expand the gh CLI token scope to allow workflow file creation
gh auth refresh -h github.com -s workflow

# 2. Push the workflow file
git add .github/workflows/build.yml
git commit -m "ci: add GitHub Actions build matrix"
git push
```

Or, alternatively, add the file via the GitHub web UI:

1. Open https://github.com/akorite/driftwood/new/master
2. Name the file `.github/workflows/build.yml`
3. Paste the contents of `.github/workflows/build.yml` from this repo
4. Click "Commit new file"

## What the workflow does

Builds and tests on every push and PR across:

| OS | Compiler | Build type |
|----|----------|------------|
| Ubuntu | GCC | Release |
| Ubuntu | Clang | Release |
| macOS | Apple Clang | Release |
| Windows | MSVC | Release |

Steps per job:

1. Checkout
2. Install build tools (apt on Linux, MSVC dev-cmd on Windows)
3. `cmake -B build -S . -DCMAKE_BUILD_TYPE=Release`
4. `cmake --build build`
5. `./driftwood perft 5` (move-generation correctness smoke test)
6. `ctest --output-on-failure` (full test suite)
7. Upload the binary as a build artifact (`driftwood-<os>-Release`)

## After enabling

The first run will take ~10–15 minutes (perft downloads CMake and
GoogleTest). Subsequent runs are faster thanks to incremental builds
and the build cache.

To trigger a build manually without a push, use the "Run workflow"
button on the Actions tab.
