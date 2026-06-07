# Contributing to DriftWood

Thanks for your interest in DriftWood! This is a hobby chess engine, so
contributions of any size are welcome — bug fixes, evaluation tuning, new
pruning ideas, test positions, and documentation improvements.

## Where to start

- **Issues tagged `good first issue`** — small, well-scoped tasks for new contributors.
- **Issues tagged `help wanted`** — bigger work items where the design is settled but the implementation is open.
- **Open a discussion** in [GitHub Discussions](https://github.com/akorite/driftwood/discussions) before sending a large PR (e.g., new evaluation feature, search heuristic). This avoids wasted work if the design direction doesn't fit.

## Development setup

```bash
# Clone
git clone https://github.com/akorite/driftwood.git
cd driftwood

# Debug build (for running tests under a debugger)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Run the full test suite
cd build && ctest --output-on-failure
```

The first build fetches GoogleTest via CMake `FetchContent` (requires internet).
Subsequent builds are incremental.

## Code style

- **C++17** — no `std::format`, concepts, or modules
- **snake_case** for functions and variables, **CamelCase** for types, **UPPER_SNAKE_CASE** for constants
- **`static`** for all functions not exported across translation units
- **No raw `new` / `delete`** — use `std::unique_ptr` if needed
- **No exceptions** in hot paths (search, move gen)
- **No heap allocation** in hot paths — search tables, history, killer tables are fixed-size arrays
- **Comment the *why*, not the *what*** — a function named `score_capture()` doesn't need a comment, but `// MVV-LVA: bishops are worth more than knights` is welcome

When in doubt, read `src/search.cpp` and match its style. The goal is code
that reads like it was written by one person, even if it wasn't.

## Commit messages

We use [Conventional Commits](https://www.conventionalcommits.org/) loosely:

```
feat(search): add counter-move history pruning
fix(serve): respect time control on the move endpoint
docs: document time-management tiers
test(eval): add regression test for bishop pair bonus
refactor(board): consolidate make/unmake helpers
chore: bump httplib to v0.16.0
```

Scope is one of: `search`, `eval`, `board`, `movegen`, `book`, `tt`, `serve`, `web`, `ci`, `docs`, `tests`.

## Pull request checklist

- [ ] All tests pass locally (`ctest --output-on-failure`)
- [ ] No new compiler warnings (`-Wall -Wextra -Wpedantic`)
- [ ] New code paths have tests (or a clear reason why not — e.g., pure refactor)
- [ ] Public API additions are documented in `docs/DESIGN.md` or the relevant header
- [ ] Commit history is clean — squash fixups, rebase onto master

## Reporting bugs

Open a [GitHub issue](https://github.com/akorite/driftwood/issues) with:

1. **Reproduction** — the exact command, FEN, or browser action that triggers the bug
2. **Expected** — what you expected to happen
3. **Actual** — what actually happened (copy-paste of error output, or a screenshot)
4. **Environment** — OS, compiler version (`g++ --version` / `clang++ --version` / MSVC `cl.exe`), build type

For engine correctness bugs (wrong move played, evaluation off), please include the FEN and a depth at which the wrong behavior shows up. Perft mismatches are especially valuable — they pinpoint move-generation or make/unmake bugs.

## License

By contributing, you agree that your contributions will be licensed under the
project's [MIT license](../LICENSE).
