<p align="center">
  <img src="docs/screenshots/web-ui-main.png" alt="DriftWood" width="880">
</p>

<h1 align="center">DriftWood</h1>

<p align="center">
  A chess engine with a built-in web UI. Play in your browser, no setup beyond one binary.
</p>

<p align="center">
  <a href="#quick-start">Quick Start</a> ·
  <a href="#play-now">Play Now</a> ·
  <a href="#engine-internals">Internals</a> ·
  <a href="#contributing">Contribute</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue?logo=cplusplus&logoColor=white" alt="C++17">
  <img src="https://img.shields.io/badge/License-MIT-green" alt="MIT License">
  <img src="https://img.shields.io/badge/Tests-54%2B-brightgreen" alt="Tests">
  <img src="https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-gray" alt="Platform">
</p>

---

A ~2400 ELO chess engine written in C++17. Single binary, no dependencies, no internet required. Built-in web UI with drag-and-drop play, clock, move history, and live eval. Connect it to any UCI GUI. Multi-threaded search, Syzygy tablebases, opening book, the works.

**Why another chess engine?** Most serious engines are CLI-only and require a separate GUI. DriftWood ships one binary that speaks UCI, has a web UI baked in, and has a clean enough codebase to read, learn from, and hack on.

```mermaid
graph TB
    subgraph "driftwood (single binary)"
        UI["Web UI<br/>chessboard.js + chess.js"]
        HTTP["HTTP Server<br/>cpp-httplib"]
        UCI["UCI Protocol"]
        SEARCH["Searcher<br/>PVS + Lazy SMP"]
        EVAL["Evaluation"]
        TT["Transposition Table"]
        BOOK["Opening Book"]
        SYZYGY["Syzygy Tablebases"]
        BOARD["Board<br/>bitboards + Zobrist"]
        MOVEGEN["Move Generation<br/>magic bitboards"]
    end

    UI <-->|REST API| HTTP
    UCI <--> SEARCH
    HTTP --> SEARCH
    SEARCH --> EVAL
    SEARCH --> TT
    SEARCH --> BOOK
    SEARCH --> SYZYGY
    EVAL --> BOARD
    SEARCH --> BOARD
    BOARD --> MOVEGEN

    style UI fill:#2d3748,stroke:#718096,color:#e2e8f0
    style HTTP fill:#2d3748,stroke:#718096,color:#e2e8f0
    style UCI fill:#2d3748,stroke:#718096,color:#e2e8f0
    style SEARCH fill:#2b6cb0,stroke:#3182ce,color:#fff
    style EVAL fill:#2b6cb0,stroke:#3182ce,color:#fff
    style TT fill:#2f855a,stroke:#38a169,color:#fff
    style BOOK fill:#2f855a,stroke:#38a169,color:#fff
    style SYZYGY fill:#2f855a,stroke:#38a169,color:#fff
    style BOARD fill:#744210,stroke:#d69e2e,color:#fff
    style MOVEGEN fill:#744210,stroke:#d69e2e,color:#fff
```

## Play Now

```bash
git clone https://github.com/akorite/driftwood.git
cd driftwood
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/driftwood serve
```

Open **http://localhost:8080**, pick a side, play.

<p align="center">
  <img src="docs/screenshots/web-ui-gameplay.png" alt="Gameplay" width="880">
</p>

### Web UI Features

| | |
|---|---|
| Drag-and-drop with legal move indicators | Clock with 1 / 3 / 10 / 30 minute presets |
| Move history in SAN (click to jump back) | Captured pieces display |
| Live evaluation + depth readout | Board flip (`F`), new game (`N`) |

Everything is vendored under `web/vendor/`. Works fully offline, no CDN, no `npm install`.

## Engine Internals

DriftWood is a real chess engine, not a toy. It has the search techniques you'd find in competitive engines, implemented in ~4K lines of readable C++17.

### Search

```mermaid
flowchart TD
    ID["Iterative Deepening<br/>depth 1, 2, 3, ..."]
    AW["Aspiration Window<br/>narrow search first"]
    PVS["PVS: Search root move"]
    ORDER["Move Ordering<br/>TT hash > MVV-LVA > Killers > History"]
    PRUNE{"Pruning checks"}
    LMR["LMR: reduce late moves"]
    NMP["Null-Move: skip side, search reduced"]
    FUTILITY["Futility: prune at frontier"]
    RAZOR["Razoring: drop to qsearch"]
    EXTEND["Check Extension: +1 ply"]
    QS["Quiescence Search<br/>captures only, stand-pat"]
    STORE["Store in TT"]
    NEXT{"More moves?"}
    DEEPER{"time_up?"}

    ID --> AW --> PVS --> ORDER --> PRUNE
    PRUNE -->|quiet + low depth| LMR
    PRUNE -->|non-endgame| NMP
    PRUNE -->|frontier, eval < alpha| FUTILITY
    PRUNE -->|very low depth| RAZOR
    PRUNE -->|in check| EXTEND
    LMR --> NEXT
    NMP --> NEXT
    FUTILITY --> NEXT
    RAZOR --> QS --> NEXT
    EXTEND --> PVS
    NEXT -->|yes| PVS
    NEXT -->|no| STORE --> DEEPER
    DEEPER -->|no| ID
    DEEPER -->|yes| DONE["Return best move"]

    style ID fill:#2b6cb0,stroke:#3182ce,color:#fff
    style PVS fill:#2b6cb0,stroke:#3182ce,color:#fff
    style QS fill:#2b6cb0,stroke:#3182ce,color:#fff
    style PRUNE fill:#c53030,stroke:#e53e3e,color:#fff
    style LMR fill:#744210,stroke:#d69e2e,color:#fff
    style NMP fill:#744210,stroke:#d69e2e,color:#fff
    style FUTILITY fill:#744210,stroke:#d69e2e,color:#fff
    style RAZOR fill:#744210,stroke:#d69e2e,color:#fff
    style EXTEND fill:#2f855a,stroke:#38a169,color:#fff
    style STORE fill:#2f855a,stroke:#38a169,color:#fff
```

### Evaluation

- Material + piece-square tables with middlegame/endgame interpolation
- Mobility (N, B, R, Q), king safety (pawn shield, attacker weights)
- Pawn structure: doubled, isolated, passed, backward, candidates
- Knight outposts, bishop pair, rook on 7th, space, threats
- Passed pawn king proximity

### Threading

```mermaid
flowchart LR
    subgraph shared["Shared State"]
        TT["Transposition Table<br/>(1-1024 MB)"]
    end

    subgraph t0["Thread 0"]
        H0["History"]
        K0["Killers"]
        C0["Countermove"]
    end

    subgraph t1["Thread 1"]
        H1["History"]
        K1["Killers"]
        C1["Countermove"]
    end

    subgraph tN["Thread N"]
        HN["History"]
        KN["Killers"]
        CN["Countermove"]
    end

    t0 <-->|read/write| TT
    t1 <-->|read/write| TT
    tN <-->|read/write| TT

    style shared fill:#2f855a,stroke:#38a169,color:#fff
    style TT fill:#2b6cb0,stroke:#3182ce,color:#fff
    style t0 fill:#2d3748,stroke:#718096,color:#e2e8f0
    style t1 fill:#2d3748,stroke:#718096,color:#e2e8f0
    style tN fill:#2d3748,stroke:#718096,color:#e2e8f0
```

**Lazy SMP**: multiple threads share one transposition table, each with private history/killers/countermover. Thread count configurable via UCI (`setoption name Threads value 4`).

### Extras

- **Opening book**: 321 entries, weighted random selection
- **Syzygy tablebases**: WDL/DTZ probing for 6-7 man endgames
- **Time management**: adaptive budgeting per clock tier, depth caps per budget

## UCI Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `Hash` | spin | 64 | Transposition table size (MB) |
| `Threads` | spin | 1 | Search threads |
| `SyzygyPath` | string | | Path to Syzygy tablebase files |
| `BookFile` | string | `books/driftwood.bin` | Opening book path |
| `BookMoves` | spin | 12 | Maximum book moves |

## HTTP API

The web UI is backed by a simple REST API. Use it to build your own frontends or integrate DriftWood into anything.

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/new_game?color=white\|black\|random` | Start a new game |
| POST | `/api/move` | Send a move, get engine reply |
| GET | `/api/state?fen=<FEN>` | Legal moves, check/mate status |
| GET | `/api/eval?fen=<FEN>&depth=N` | Evaluation with PV line |

## CLI

```bash
./build/driftwood                    # UCI mode (default)
./build/driftwood serve [port]       # Web UI on :8080
./build/driftwood perft 5            # Move generator verification
./build/driftwood bench 12           # Benchmark (nodes/sec)
./build/driftwood selfplay 20        # Engine vs. itself
```

## Tests

```bash
cd build && ctest --output-on-failure
```

54+ tests covering move generation, evaluation correctness, search tactics, TT behavior, Lazy SMP determinism, and SEE.

## Building

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Requirements:** C++17 (GCC 9+ / Clang 10+ / MSVC 2019+), CMake 3.16+. That's it. No Boost, no external libs. GoogleTest is fetched automatically for the test suite.

**Cross-platform:**
- **Linux / macOS**: `gcc` / `clang`
- **Windows**: MSVC or MinGW

## Contributing

Contributions welcome. Bug fixes, evaluation tuning, new pruning ideas, test positions, docs.

See [CONTRIBUTING.md](CONTRIBUTING.md) for setup, code style, and PR checklist. Open an issue or discussion before large changes.

## License

MIT
