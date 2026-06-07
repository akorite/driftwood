# DriftWood

A classical chess engine built from scratch in **C++17**, targeting expert strength (2300–2500 ELO). Uses bitboard-based board representation, handcrafted evaluation, principal-variation search with pruning, and Lazy SMP multithreading.

## Phase 1: Foundation

The foundation implements:

- **Bitboard board representation** with 12 piece bitboards (6 piece types × 2 colors)
- **Zobrist hashing** for fast position hashing (incremental update on make/unmake)
- **FEN parsing and output** for position I/O
- **Full legal move generation** including castling (kingside/queenside), en passant, all four promotion types
- **Perft** (performance test) with bulk counting for validation
- **Perft split** showing per-move node counts for debugging
- **Precomputed attack tables** (knight, king, pawn) initialized via static construction
- **CLI** with `perft`, `fen`, and `perftsuite` subcommands
- **GoogleTest** test suite with 26 perft assertions across 6 positions

## Phase 2: Search, Evaluation, and UCI

Phase 2 adds a full-strength search engine and UCI protocol:

- **Transposition table** (64 MB default, configurable 1–1024 MB) with Zobrist hashing and depth-preferred replacement
- **Evaluation**: material balance (P=100, N=320, B=330, R=500, Q=900), midgame and endgame piece-square tables, phase-based interpolation (24 = max phase), tempo bonus
- **Principal Variation Search** (PVS / NegaScout) with iterative deepening and aspiration windows
- **Move ordering**: TT hash move, MVV-LVA captures, killer moves (2 per ply), countermove heuristic, history heuristic
- **Pruning**: late-move reductions (LMR), null-move pruning (R=3 with verification), futility pruning (depth ≤ 3), razoring (depth ≤ 2)
- **Quiescence search** with stand-pat, MVV-LVA capture ordering, and delta pruning
- **Check extension** (extend by 1 ply when in check)
- **Time management**: supports fixed `movetime`, classical time controls (`wtime`/`btime`/`winc`/`binc`), and `infinite` mode
- **UCI protocol**: `uci`, `isready`, `position`, `go` (depth/nodes/movetime/time controls/infinite), `stop`, `quit`, `ucinewgame`, `setoption`, and custom `print`
- **CLI**: `perft`, `fen`, `perftsuite`, `bench` (benchmark across 6 positions), `selfplay` (auto-play N moves)
- **GoogleTest** test suite expanded to 38 tests (eval, TT probe/replace, search tactics)

## Phase 3: Positional Evaluation, Opening Book, Syzygy

Phase 3 adds:

- **Positional evaluation**: mobility (knights, bishops, rooks, queens), king safety (pawn shield, open files near king, attacker weights), pawn structure (doubled, isolated, passed, backward, candidate passers), outposts for knights, bishop-pair bonus
- **Opening book**: weighted random selection from 321 entries (167 positions)
- **Syzygy endgame tablebases**: WDL/DTZ probing for 6–7 man positions with configurable path
- **GoogleTest** test suite expanded to >38 tests

## Phase 4: Lazy SMP Multithreading

Phase 4 adds Lazy SMP multithreading for parallel search:

- **Lazy SMP**: multiple threads share a single transposition table and run iterative deepening independently. The TT acts as the sole communication channel — when one thread finds a beta cutoff, the result is stored in the TT and picked up by other threads.
- **Per-thread state**: each thread has its own history, killer, countermove, and search stack tables (no heap allocation in hot path).
- **TT locking**: a per-instance mutex protects the TT during probe and store operations.
- **Thread count**: configurable via UCI option `Threads` (spin, default 1, min 1, max 256).
- **Bench reporting**: the `bench` command reports nodes-per-second including all threads.
- **New tests**: 4 tests covering 1-thread parity, multi-thread smoke, stop-flag safety, and deterministic single-thread results.

## Phase 5: Web UI

Phase 5 adds a Lichess-like web interface for playing against the engine, served directly by the engine binary.

- **Embedded HTTP server** using [cpp-httplib](https://github.com/yhirose/cpp-httplib) (vendored at `third_party/httplib.h`)
- **Lichess-style UI** using [chessboard.js](https://github.com/oakmac/chessboardjs) (board, MIT) and [chess.js](https://github.com/jhlywa/chess.js) (move logic, BSD-2). All third-party JS and the cburnett piece set are vendored under `web/vendor/` — no runtime CDN dependency, works fully offline / behind a proxy.
- **4 API endpoints** for the UI to talk to the engine
- **Single binary** — `driftwood serve` starts both the engine and the web server
- **Opening book support** — book moves are used when available
- **Time control** — 1/3/10/30 minute presets, 10+0 default

## Requirements

- **C++17 compiler** (GCC 9+, Clang 10+, MSVC 2019+)
- **CMake 3.16+**
- **pthread** (Linux, macOS) — required by std::thread and httplib
- No other dependencies — GoogleTest is fetched automatically via CMake FetchContent. The web UI is self-contained; all third-party JS and the piece set are vendored under `web/vendor/`.

## Building

```bash
# Configure (debug)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Configure (release — faster perft and search)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j$(nproc)
```

## Running

### UCI mode (default, no arguments)

```bash
# Start the engine in UCI mode
./build/driftwood

# Or pipe UCI commands
echo "uci" | ./build/driftwood
echo -e "position startpos moves e2e4\ngo depth 5\nquit" | ./build/driftwood
```

### Web UI (serve mode)

```bash
# Start the web UI on the default port (8080)
./build/driftwood serve

# Or specify a custom port
./build/driftwood serve 9090
```

Then open http://localhost:8080 in your browser. You'll see a Lichess-like interface:

```
┌──────────────────────────────────────────────────┐
│  DriftWood                            Eval: -    │
├──────────┬───────────────────────┬───────────────┤
│ Opponent │                       │ Move History  │
│ 10:00    │                       │ 1. e4    ...  │
│ [■■]     │      [BOARD]         │ 2. Nf3   ...  │
│          │                       │ 3. Bb5   ...  │
│ You      │                       │               │
│ 10:00    │                       │  [Flip Board] │
│ [■■]     │                       │               │
├──────────┴───────────────────────┴───────────────┤
│ [New Game] [Play White ▼] [Resign] [Draw]        │
└──────────────────────────────────────────────────┘
```

The UI features:
- **Drag-and-drop** piece movement using chessboard.js
- **Legal-destination dots** that appear when you pick up a piece (filled dot for empty, ring for capture)
- **Clock** (1/3/10/30 min presets) counting down for the side to move
- **Move history** in SAN notation, scrollable and clickable to revisit positions
- **Captured pieces** display (Unicode glyphs) for both sides
- **New Game** with white/black/random color choice
- **Resign** and **Draw** buttons
- **Evaluation display** (updated every 4 seconds)
- **Check indicator** and **last-move highlight** (yellow-green)
- **Board flip** button and keyboard shortcuts
- **Keyboard shortcuts**: `N` for new game, `F` to flip board, `Escape` to close modal

### CLI subcommands

```bash
# Perft on the starting position at depth 5
./build/driftwood perft 5

# Perft on a custom FEN
./build/driftwood perft 4 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"

# Perft split (show per-move counts)
./build/driftwood perft 3 --split

# Run the full perft suite
./build/driftwood perftsuite

# Normalize a FEN string
./build/driftwood fen "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

# Benchmark (search 6 positions at depth N)
./build/driftwood bench 8

# Self-play N moves from startpos (or custom FEN)
./build/driftwood selfplay 10
./build/driftwood selfplay 20 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"

# Usage info
./build/driftwood --help
```

### HTTP API Endpoints

The server exposes four endpoints for the UI:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/new_game?color=white\|black\|random` | GET | Start a new game, returns initial FEN and game ID |
| `/api/move` | POST | Make a move or get engine response |
| `/api/state?fen=<FEN>` | GET | Get position state (legal moves, check, mate, etc.) |
| `/api/eval?fen=<FEN>&depth=N` | GET | Get engine evaluation with PV |

**`/api/new_game`** response:
```json
{
  "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "side_to_move": "w",
  "game_id": "1",
  "human_color": "white",
  "engine_move": ""
}
```

**`/api/move`** request body:
```json
{ "fen": "<current FEN>", "move": "e2e4" }
```
The `move` field is optional. If omitted, the engine just evaluates the position.
Response:
```json
{
  "legal": "true",
  "fen": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
  "user_move": "e2e4",
  "engine_move": "e7e5",
  "is_check": "false",
  "is_checkmate": "false",
  "is_game_over": "false",
  "result": "*"
}
```

**`/api/state`** returns legal moves, check/checkmate/stalemate status, and game result.

**`/api/eval`** returns the engine's evaluation (score in centipawns), depth, PV, and NPS.

### Using multiple threads

```bash
# In UCI mode:
echo -e "setoption name Threads value 4\nposition startpos\ngo depth 10\nquit" | ./build/driftwood

# Or interactively:
# uci
# setoption name Threads value 4
# isready
# position startpos
# go movetime 10000
```

## Testing

```bash
# Run all tests via ctest
cd build && ctest --output-on-failure

# Or run individual test binaries
./build/tests/perft_test      # 26 perft assertions
./build/tests/engine_test     # >40 eval, TT, search, and multithreading tests
```

## UCI Options

| Option | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| `Hash` | spin | 64 | 1–1024 | Transposition table size in MB |
| `Threads` | spin | 1 | 1–256 | Number of search threads (Lazy SMP) |
| `SyzygyPath` | string | \<empty\> | — | Path to Syzygy tablebase files |
| `BookFile` | string | books/driftwood.bin | — | Path to opening book file |
| `BookMoves` | spin | 12 | 0–100 | Max book moves to consider |

## Verification

### Perft (move-generation correctness)

| Depth | Nodes     |
|-------|-----------|
| 1     | 20        |
| 2     | 400       |
| 3     | 8,902     |
| 4     | 197,281   |
| 5     | 4,865,609 |

### Perft suite results (all pass)

| Position | Depth | Nodes |
|----------|-------|-------|
| Starting | 5 | 4,865,609 |
| Kiwipete | 4 | 4,085,603 |
| Pos 3 | 5 | 674,624 |
| Pos 4 | 4 | 422,333 |
| Pos 5 | 4 | 2,103,487 |
| Pos 6 | 4 | 3,894,594 |

### Tactical tests (all pass)

- **Mate detection**: Scholar's mate detected as checkmate
- **Capture finding**: Rook captures hanging pawn (depth 3)
- **Queen tactics**: Queen takes undefended pawn when safe; retreats when capture is dangerous
- **Promotion**: Pawn push to promotion found (depth 3)

### Lazy SMP verification

- **1-thread parity**: `search_parallel(1)` produces the same result as `search()` on tactical positions
- **Multi-thread smoke**: 4-thread search at depth 6 on the starting position completes without error
- **Stop flag**: Setting the stop flag mid-search exits cleanly
- **Determinism**: Single-thread searches produce deterministic results

## Project Structure

```
├── CMakeLists.txt              # Top-level build configuration
├── third_party/
│   └── httplib.h               # cpp-httplib (single-header HTTP library)
├── include/driftwood/          # Public headers (library API)
│   ├── types.hpp               # Color, PieceType, Square, Move, CastlingRights, MoveList
│   ├── board.hpp               # Board representation, Zobrist, FEN, make/unmake
│   ├── movegen.hpp             # Move generation, attack detection
│   ├── perft.hpp               # Perft/perft-split functions
│   ├── tt.hpp                  # Transposition table (Zobrist hashing)
│   ├── eval.hpp                # Material + PST evaluation
│   ├── search.hpp              # PVS search, iterative deepening, pruning, Lazy SMP
│   ├── book.hpp                # Opening book
│   ├── syzygy.hpp              # Syzygy tablebase probing
│   ├── uci.hpp                 # UCI protocol handler
│   └── serve.hpp               # HTTP server entry point
├── src/                        # Implementation
│   ├── types.cpp
│   ├── board.cpp               # Board implementation, Zobrist table, null move
│   ├── movegen.cpp             # Move generation, attack tables
│   ├── perft.cpp               # Perft counting and suite runner
│   ├── tt.cpp                  # Transposition table
│   ├── eval.cpp                # Material + PST + phase interpolation
│   ├── search.cpp              # PVS, qsearch, LMR, NMP, futility, razoring, Lazy SMP
│   ├── book.cpp                # Opening book
│   ├── syzygy.cpp              # Syzygy tablebase probing
│   ├── uci.cpp                 # UCI protocol loop
│   ├── serve.cpp               # HTTP server (serve mode)
│   └── main.cpp                # CLI entry point (UCI by default, serve subcommand)
├── web/                        # Web UI (static files)
│   ├── index.html              # Single-page app
│   ├── style.css               # Lichess-inspired dark theme
│   ├── app.js                  # UI logic (chessboard.js + chess.js)
│   └── vendor/                 # Vendored third-party assets
│       ├── jquery.min.js       # jQuery 3.7.1 (MIT) - chessboard.js dep
│       ├── chessboard.js       # chessboard.js 1.0.0 (MIT) - board widget
│       ├── chessboard.css      # chessboard.js base CSS
│       ├── chess.js            # chess.js 0.13.4 (BSD-2) - move logic, with UMD shim
│       ├── LICENSES.md         # Third-party license attributions
│       └── img/chesspieces/wikipedia/  # cburnett piece set (CC BY-SA 3.0)
├── tests/
│   ├── CMakeLists.txt          # Test build configuration
│   ├── perft_test.cpp          # 26 perft assertions
│   └── engine_test.cpp         # Eval, TT, search, Lazy SMP tests
└── docs/                       # Design docs and requirements
```

## Philosophy

- **Correctness first** — all rules of chess must be faithfully implemented
- **Zero heap allocation in hot paths** — move generation, search tables, and history use fixed-size arrays
- **No unnecessary abstractions** — bitboards are raw `uint64_t`, moves are packed `uint32_t`, search tables are plain arrays
- **Incremental optimization** — simplicity before magic bitboards and advanced pruning

## What's Next (beyond Phase 5)

- **Strength target**: 2300–2500 ELO (evaluation tuning)
- **Analysis board**: show engine's live PV/eval during play
- **Premoves**: support for premoves in the UI
- **Sound effects**: Lichess-style move/capture sounds
- **PGN export**: download/display game PGN
- **Multiple time controls**: configurable time presets in the UI
- **Syzygy in serve mode**: expose tablebase info in the eval endpoint
- **Multiple concurrent games**: game tabs or separate game sessions

## License

MIT — see [LICENSE](LICENSE).
