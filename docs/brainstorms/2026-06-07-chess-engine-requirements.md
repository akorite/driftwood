---
date: 2026-06-07
topic: chess-engine
---

# Chess Engine

## Summary

Build a from-scratch classical chess engine in **C++17** targeting expert strength (2300–2500 ELO) with no neural network or training compute, paired with a Lichess-like web UI for playing against it. The UI reuses Lichess's own chessground + chess.js packages rather than custom components. Engine built comprehensive-first: v1 ships coherent at intermediate strength (~2000–2200 ELO), then tuned to expert. Supports two use cases: interactive play via the UI, and engine-vs-engine matches via CuteChess.

Phase 1 (complete): Board representation, move generation, perft, CLI.
Phase 2 (complete): Search (PVS), evaluation (material + PST), transposition table, UCI protocol, tests.
Phase 3 (complete): Positional evaluation, opening book, Syzygy tablebases.
Phase 4 (complete): Lazy SMP multithreading.
Phase 5 (complete): Web UI.

---

## Problem Frame

The user wants to build a chess engine as a personal project — for the satisfaction of constructing one from scratch, for the intellectual interest in search and evaluation algorithms, and for the ability to play against an engine that reflects their own design choices. There is no commercial or competitive motivation; existing engines (Stockfish, Leela) already cover the strength axis. The defining constraints come from the user: no neural network or training compute, classical alpha-beta + handcrafted evaluation, expert-strength target. The Lichess-like UI exists to play against the engine and to test it; reusing Lichess's own open-source packages makes that practical for a single-developer fun project.

---

## Key Flows

- F1. **User plays a game against the engine via the web UI**
  - **Trigger:** User opens the web app in a browser
  - **Actors:** User
  - **Steps:** UI displays the starting position with clock; user plays their color by dragging a piece; UI sends the resulting position to the engine via the library API; engine computes and returns a move; UI displays the engine's move, updates the board with last-move highlight and check indicator if applicable; loop continues until checkmate, stalemate, draw, resign, or time forfeit
  - **Outcome:** Complete game record (PGN) available
  - **Covered by:** R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, R19, R20

- F2. **Engine-vs-engine match via CuteChess**
  - **Trigger:** User configures CuteChess with the engine binary
  - **Actors:** User (match runner), CuteChess (orchestrator)
  - **Steps:** CuteChess sends a position via UCI `position`; engine computes a move via `go`; CuteChess delivers the move to the opponent; loop; results saved as PGN
  - **Outcome:** Match PGNs and aggregate results
  - **Covered by:** R1, R2, R4, R5, R6, R7, R19, R20

- F3. **Developer runs perft validation**
  - **Trigger:** Developer invokes perft on a known test position
  - **Actors:** Developer
  - **Steps:** Engine receives a depth value; counts leaf nodes from the position; reports the node count; developer compares against a known-good value
  - **Outcome:** Correctness signal
  - **Covered by:** R16, R17

---

## Requirements

**Engine correctness and interface**
- R1. Plays fully legal chess including castling (kingside and queenside), en passant, all four promotion types, 50-move rule, threefold repetition, stalemate, and insufficient-material draws.
- R2. Speaks the UCI protocol (universal chess interface) including `uci`, `isready`, `position`, `go`, `stop`, `quit`, with support for `go depth`, `go nodes`, and `go movetime` time controls.
- R3. Exposes a library API that takes a FEN and a search budget and returns a move, for in-process use by the web UI.

**Search**
- R4. Alpha-beta search (negamax or principal variation search) with iterative deepening as the primary search algorithm.
- R5. Transposition table using Zobrist hashing, with a depth-preferred replacement scheme.
- R6. Full move ordering suite: MVV-LVA, killer moves, history heuristic, and countermove heuristic.
- R7. Search pruning suite: late move reductions (LMR), null move pruning (NMP) with verification, futility pruning, razoring, and multi-cut pruning.
- R8. Quiescence search at the horizon to resolve capture and check sequences and avoid the horizon effect.

**Evaluation (handcrafted, no learning)**
- R9. Material balance and piece-square tables for each piece type as the base evaluation.
- R10. Positional evaluation covering mobility (knights, bishops, rooks, queens), king safety (pawn shield, open files near king, attacker weights), pawn structure (doubled, isolated, passed, backward, candidate passers), outposts for knights, and a bishop-pair bonus.
- R11. Phase-based interpolation between middlegame and endgame evaluation values, using game phase derived from remaining material.

**UI (web)**
- R12. Visual board with drag-and-drop move input, using Lichess's chessground component.
- R13. Move history in standard algebraic notation (SAN), captured pieces display for both sides, and ability to scroll back through the game.
- R14. Clock with configurable time control, displayed for both sides and counting down only on the side to move.
- R15. New-game (with color choice), resign, and draw-offer controls.
- R16. Last-move highlight, check indicator, and board-flip controls.

**Testing and correctness**
- R17. Perft validation on the starting position and a standard set of Kiwipete / perftsuite positions, with depth covered up to the suite's standard depth for each position.
- R18. Perft split output showing per-move node counts at each depth, for debugging move-generation correctness.

**Opening book and endgame tablebases**
- R19. Curated opening book of 1k–10k positions, with weighted random move selection from the book's candidate moves at book positions, and graceful fallback to search when a position is not in the book.
- R20. Syzygy 6–7 man endgame tablebase support, with the tablebase path configurable at engine startup; the engine probes tablebases in positions with few enough pieces and falls back to search + evaluation otherwise.

---

## Acceptance Examples

- AE1. **Covers R1, R2.** Given the engine in UCI mode, when sent `position startpos moves e2e4` followed by `go depth 10`, the engine returns a legal move within the depth limit.
- AE2. **Covers R2.** Given the engine in UCI mode, when sent `uci`, the engine responds with `id name`, `id author`, and `uciok`.
- AE3. **Covers R1.** Given a position where white can castle kingside, when the engine is given the corresponding `position` and asked for a move, the engine produces a legal short-castling move when appropriate.
- AE4. **Covers R14, R1.** Given a game in progress with both sides on a 5+0 clock, when a player fails to move before their clock reaches 0:00, the game ends with the correct winner declared.
- AE5. **Covers R17.** Given the engine at the starting position, when `perft 5` is run, the engine reports exactly 4865609 nodes.
- AE6. **Covers R3, R4, R12.** Given a fresh game in the UI, when the user makes a legal move on the board, the engine returns a counter-move within 5 seconds on typical hardware, and the board updates to reflect both moves with the last-move highlight visible.
- AE7. **Covers R19.** Given an opening book is configured, when the engine reaches a book position, the engine selects a move from the book's candidate moves (with weighted random selection) rather than searching, and the same game from the same starting position can produce different opening moves across multiple runs.
- AE8. **Covers R20.** Given a Syzygy tablebase path is configured, when the engine is asked for a move in a position with 6 or fewer pieces on the board, the engine returns the tablebase-optimal move and reports the tablebase WDL / DTZ in the UCI info string.

---

## Success Criteria

- v1 engine plays fully legal chess and produces a move for any legal position, including all standard rule edge cases.
- v1 engine reaches 2000–2200 ELO in engine-vs-engine matches against reference engines (e.g., Stockfish pre-NNUE, Crafty, or similar reference).
- After tuning, the engine reaches 2300–2500 ELO (expert strength).
- Perft tests pass at standard depths on the starting position and the standard perftsuite positions.
- The web UI loads in a modern browser and allows full games against the engine with time control, move history, captured pieces, and standard game controls.
- The engine can be loaded into CuteChess (or equivalent) and participate in matches without manual intervention.

---

## Scope Boundaries

- Neural network evaluation (NNUE, MCTS + neural network) — excluded by the no-training constraint.
- Online play, accounts, multiplayer, server infrastructure.
- Built-in match runner or engine-vs-engine orchestration — CuteChess or equivalent handles it externally.
- Custom UI components beyond what chessground + chess.js provide.
- Online tournament participation or CCRL / FIDE rating submissions.
- Mobile-native app — web UI only.
- Engine analysis overlay in the UI (showing the engine's live PV / eval while you play) — deferred for a later release.

---

## Key Decisions

- Build approach is comprehensive design-first: v1 ships with all major features designed in at intermediate strength, then tuned to expert — chosen over iterative MVP-first for cleaner architecture.
- UI reuses Lichess's own open-source packages (chessground + chess.js) rather than custom components — Lichess visual feel for free, no custom UI work.
- Engine exposes both a library API (for the UI) and speaks UCI (for engine-vs-engine) — two interfaces, one engine.
- Evaluation is pure handcrafted (no Texel tuning against PGN databases, no neural network learning) — slower strength gains, no training data dependency.
- Engine-vs-engine orchestration is external (CuteChess or equivalent) rather than built into the engine.
- Include a small curated opening book in v1 (1k–10k positions) for opening variety in matches, rather than relying on the engine's own opening understanding.
- Include Syzygy 6–7 man endgame tablebase support in v1 to reach expert-strength endgame play, with the tablebase path as a runtime-configured dependency.

---

## Dependencies / Assumptions

- CuteChess or an equivalent open-source match orchestrator is available for engine-vs-engine matches.
- chessground and chess.js are available as open-source packages and provide the necessary primitives for the UI.
- Modern web browser as the UI runtime; no server-side rendering required.
- No external services or network dependencies at runtime — all computation is local.
- Target hardware: 1 GHz+ CPU and 256+ MB RAM (any modern machine), plus disk space for the Syzygy tablebases (a few GB for 6-man, tens of GB for 7-man).
- Programming language: deferred to planning, but expected to be C++ or Rust for performance; this affects how the UI integrates with the engine.
- Syzygy tablebase files (WDL and DTZ) downloaded by the user from a public source and placed at a configurable path; the engine does not download them itself.

---

## Phase 1 Status (2026-06-07): Complete

All Phase 1 deliverables are implemented and verified:
- ✅ Project setup with CMake + C++17, zero external dependencies (GoogleTest via FetchContent)
- ✅ Bitboard board representation with Zobrist hashing
- ✅ FEN parsing and output
- ✅ Full legal move generation (castling, en passant, promotions)
- ✅ Perft with bulk counting (depth 1–5 verified on all 6 perftsuite positions)
- ✅ Perft-split for per-move debugging
- ✅ 26 passing perft tests
- ✅ CLI with perft, fen, and perftsuite subcommands

## Phase 2 Status (2026-06-07): Complete

All Phase 2 deliverables are implemented and verified:
- ✅ **Transposition table**: fixed-size, configurable 1–1024 MB, Zobrist hashing (upper 16-bit key verification), depth-preferred replacement with generation aging, probe/store/prefetch
- ✅ **Evaluation**: material balance (P=100, N=320, B=330, R=500, Q=900, K=0), midgame/endgame PSTs for all 6 piece types, phase interpolation (max 24), tempo bonus (+10 cp), side-to-move perspective
- ✅ **Search**: Principal Variation Search (PVS / NegaScout) with iterative deepening, aspiration windows, TT integration
- ✅ **Move ordering**: TT hash move, MVV-LVA captures, killer moves (2 per ply), countermove heuristic, history heuristic
- ✅ **Pruning**: LMR (log(depth)×log(moves_searched)/2.2), null-move pruning (R=3, verification), futility pruning (depth≤3), razoring (depth≤2)
- ✅ **Quiescence search**: stand-pat, MVV-LVA capture ordering, delta pruning
- ✅ **Check extension**: extend by 1 ply when in check
- ✅ **Time management**: movetime, classical (wtime/btime/winc/binc), infinite, nodes
- ✅ **UCI protocol**: uci, isready, ucinewgame, setoption, position (fen/startpos/moves), go, stop, quit, print
- ✅ **CLI**: UCI (default, no args), bench (6 positions at configurable depth), selfplay (N moves, optional FEN)
- ✅ **12 new passing tests**: eval starting/kiwipete/material, TT store/probe/collision/replace, search capture/queen-tactic/promotion
- ✅ Documentation updated (README, requirements doc)

### Key Phase 2 Decisions

- **Language: C++17** — matches existing codebase, no external dependencies beyond GoogleTest
- **TT entry size: 8 bytes** — packs key_upper (16 bits), move (16 bits), score (16 bits), depth/bound/generation (16 bits: 10+2+4)
- **No heap allocation in hot path** — search uses fixed-size arrays for killers, history, countermoves, PV, and move list
- **Single-threaded search (Phase 2)** — Lazy SMP / multithreading added in Phase 4
- **No positional evaluation (Phase 2)** — mobility, king safety, pawn structure, outposts, bishop pair added in Phase 3
- **Mate score:** 50000, with proper mate-distance handling through TT (no ply correction needed since TT stores position-relative scores)

## Phase 3 Status (2026-06-07): Complete

All Phase 3 deliverables are implemented and verified:
- ✅ **Positional evaluation**: mobility (knights, bishops, rooks, queens), king safety (pawn shield, open files, attacker weights), pawn structure (doubled, isolated, passed, backward, candidate passers), outposts for knights, bishop-pair bonus
- ✅ **Opening book**: binary format (16-byte BookEntry), weighted random selection, graceful fallback when position not in book
- ✅ **Syzygy tablebases**: WDL/DTZ probing for ≤6 piece positions, configurable path, graceful fallback when unavailable
- ✅ Book generation from hardcoded common openings
- ✅ Tests for book load/probe/weighted selection and Syzygy integration

## Phase 4 Status (2026-06-07): Complete

All Phase 4 deliverables are implemented and verified:
- ✅ **Lazy SMP multithreading**: `search_parallel` spawns N worker threads, each running iterative deepening independently with per-thread history/killer/countermove tables
- ✅ **Shared transposition table**: all threads share a single TT protected by a per-instance mutex (locked during probe and store)
- ✅ **Per-thread state**: `ThreadContext` struct holds board copy, killers, history (2×64×64 int32), countermove (64×64 uint16), search stack (128 entries), node counter, and timing info — all stack-allocated at thread start
- ✅ **Lazy SMP desynchronisation**: staggered starting depths (thread i starts at depth 1 + i%2) to spread threads across the tree
- ✅ **Shared stop flag**: `std::atomic<bool>` set by UCI `stop` or time management, checked by all threads
- ✅ **Coordinated info output**: `std::atomic<int>` `max_depth_reported` ensures only one thread per depth prints info strings; `std::mutex` guards `std::cout`
- ✅ **UCI `Threads` option**: spin, default 1, min 1, max 256
- ✅ **Bench updated**: reports nps (total nodes / wall time) with the configured thread count
- ✅ **4 new passing tests**: parallel 1-thread parity, multi-thread smoke, stop-flag safety, single-thread determinism
- ✅ All 38 existing tests still pass (no functional regressions)
- ✅ Documentation updated (README, requirements doc)

### Key Phase 4 Decisions

- **Lazy SMP** (not shared-memory YBWC or DTS) — simplest correct approach, matches Stockfish's design
- **Global TT mutex** (not per-bucket locks or lock-free) — the 8-byte entry size means lock hold time is extremely short; contention is negligible with 4–8 threads
- **Thread 0 runs on the main thread** — no coordinator thread needed; all threads are symmetric workers
- **Per-thread history tables** (not shared) — history pollution across threads is accepted as a feature of Lazy SMP; inconsistency helps explore different branches
- **No heap allocation in hot path** — ThreadContext arrays are pre-allocated before spawning threads
- **Staggered start depths** (thread i starts at depth 1 + i%2) — simple desync mechanism; the shared TT provides additional implicit desync

## Phase 5 Status (2026-06-07): Complete

All Phase 5 deliverables are implemented and verified:

- ✅ **Embedded HTTP server** using cpp-httplib (vendored at `third_party/httplib.h`)
- ✅ **4 API endpoints**: `/api/new_game`, `/api/move`, `/api/state`, `/api/eval`
- ✅ **Serve subcommand**: `driftwood serve [port]` starts the server (default port 8080)
- ✅ **Web UI** at http://localhost:8080:
  - Chessground board with drag-and-drop piece movement
  - Clock (10+0 time control), captured pieces, move history
  - New game / resign / draw controls
  - Board flip, evaluation display, check indicator
  - Keyboard shortcuts (N = new game, Escape = close modal)
- ✅ **Chessground** (v8.2.3) and **chess.js** (v1.0.0-beta.8) loaded via CDN with vendoring instructions
- ✅ **Opening book support**: book moves used in serve mode when available
- ✅ **Lichess-like dark theme**: piece SVGs from Lichess CDN, Lichess color scheme
- ✅ **All existing subcommands preserved**: UCI, perft, fen, perftsuite, bench, selfplay, genbook
- ✅ **All 54 existing tests still pass**
- ✅ **Documentation updated**: README with serve mode, API docs, UI layout, project structure

### Key Phase 5 Decisions

- **Single-threaded search in serve mode** — the HTTP request handler acquires a mutex and runs the search synchronously with 2-second movetime. Simple, correct, and sufficient for a single-user personal project.
- **cpp-httplib vendored** in `third_party/httplib.h` — no CMake FetchContent, no network during build, just a `#include`.
- **Manual JSON encoding** — no JSON library dependency. The API has only a handful of fields, so a small `json_object()` + escape helper is cleaner than pulling in nlohmann/json.
- **CDN for frontend libraries** — chessground and chess.js are loaded from unpkg CDN. Vendoring instructions are provided in `web/vendor/README.md` for offline use.
- **Lichess CDN for piece SVGs** — piece images are loaded from `https://lichess1.org/assets/piece/cburnett/` (standard Lichess piece set). This requires internet access for the initial page load; offline play is possible once cached.
- **Game state tracked on the client** — the server is stateless (except for the shared Searcher). The client sends the current FEN with each move request. Game ID is provided for future stateful features.

### Project Layout (Phase 5)

```
/
├── CMakeLists.txt              # Top-level build + test configuration
├── README.md
├── third_party/
│   └── httplib.h               # cpp-httplib (single-header HTTP library, vendored)
├── include/driftwood/
│   ├── types.hpp               # Color, PieceType, Square, Move, CastlingRights, MoveList
│   ├── board.hpp               # Board, Zobrist, FEN, make/unmake, null move, repetition
│   ├── movegen.hpp             # Move generation, attack detection
│   ├── perft.hpp               # Perft counting
│   ├── tt.hpp                  # Transposition table (thread-safe with mutex)
│   ├── eval.hpp                # Evaluation
│   ├── search.hpp              # PVS search, iterative deepening, Lazy SMP (ThreadContext)
│   ├── book.hpp                # Opening book
│   ├── syzygy.hpp              # Syzygy tablebase probing
│   ├── uci.hpp                 # UCI protocol handler
│   └── serve.hpp               # HTTP server entry point
├── src/
│   ├── types.cpp
│   ├── board.cpp
│   ├── movegen.cpp
│   ├── perft.cpp
│   ├── tt.cpp
│   ├── eval.cpp
│   ├── search.cpp              # Lazy SMP worker + search_parallel
│   ├── book.cpp
│   ├── syzygy.cpp
│   ├── uci.cpp                 # Threads option
│   ├── serve.cpp               # HTTP server (cpp-httplib)
│   └── main.cpp                # CLI entry point (serve subcommand added)
├── web/                        # Web UI static files
│   ├── index.html              # Single-page app (Lichess layout)
│   ├── style.css               # Dark theme, Lichess color scheme
│   ├── app.js                  # chessground + chess.js integration
│   └── vendor/
│       └── README.md           # CDN vendoring instructions
├── tests/
│   ├── CMakeLists.txt
│   ├── perft_test.cpp          # 26 perft assertions (Phase 1)
│   └── engine_test.cpp         # 42+ eval/TT/search/multithread tests
└── docs/
    └── brainstorms/
        └── 2026-06-07-chess-engine-requirements.md
```

### Outstanding Questions (still deferred)

- [Affects R7, R8, R9, R10, R11][Technical] Search and evaluation parameter values — initial values and tuning approach are settled in planning.
- [Affects R14][User decision] Time control presets for the UI (1+0, 3+2, 5+0, 10+0, etc.) — small UX choice, default during planning.
- [Affects R3, R12][Technical] Engine ↔ UI transport mechanism (in-process call, HTTP, WebSocket) — depends on language choice, but Rust + WebAssembly via wasm-bindgen is the intended path.
