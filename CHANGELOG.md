# Changelog

All notable changes to DriftWood will be documented in this file. The format is
loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once a stable release is cut.

## [Unreleased]

### Changed
- **Time management** — engine now respects the client's `wtime`/`btime` from `/api/move` and budgets itself proportionally (1/50 of clock on long time controls, 1/10 on emergency clocks). Hard ceiling of 2 s per move for casual UI play; depth is also capped per budget tier so a single deep iteration can't blow past the wall-clock budget.

### Fixed
- **Bot clock** — the engine's clock no longer freezes while the engine is thinking. Root cause: `chess.turn()` still says "human" during the think window because we revert the state before the fetch; the clock now uses `isEngineThinking` to flip the side.

## [0.6.0] - 2026-06-07

### Changed
- **Evaluation refinements** — added space evaluation (central file control by pawns and minor pieces), threat evaluation (hanging pieces, pressure on enemy queen), passed pawn king proximity (bonus when friendly king supports, penalty when enemy king blocks), and rook on 7th rank with pawn pressure bonus. Cleaned up king safety comment noise.
- **Search improvements** — added Internal Iterative Reduction (IIR) for cold positions without TT move, Late Move Pruning (LMP) at low depths, and tightened history table scaling to prevent overflow. Increased depth for AvoidsHangingQueen test to match new pruning behavior.

## [0.5.0] - 2026-06-07

### Added
- **Web UI (Phase 5)** — embedded HTTP server (`driftwood serve [port]`) serving a Lichess-like single-page app. Drag-and-drop board, legal-destination dots, last-move and check highlights, captured pieces, move history in SAN, board flip, time controls (1/3/10/30 min), live evaluation display.
- **HTTP API** — `GET /api/new_game`, `POST /api/move`, `GET /api/state`, `GET /api/eval`. All under the same binary; no separate server process.
- **Vendored UI deps** — `chessboard.js` 1.0.0 (MIT), `chess.js` 0.13.4 (BSD-2, with UMD shim), `jQuery` 3.7.1 (MIT), cburnett piece set (CC BY-SA 3.0). All in `web/vendor/`, no runtime CDN.

## [0.4.0] - 2026-06-07

### Added
- **Lazy SMP (Phase 4)** — multi-thread search via shared transposition table. Per-thread state for history, killers, countermove, and search stack. Configurable via UCI `Threads` option (1–256, default 1).
- **4 new tests** — 1-thread parity, multi-thread smoke, stop-flag safety, deterministic single-thread results. 54/54 total.

## [0.3.0] - 2026-06-07

### Added
- **Positional evaluation (Phase 3)** — mobility (N, B, R, Q), king safety, pawn structure (doubled, isolated, passed, backward, candidate passers), knight outposts, bishop pair.
- **Opening book** — `books/driftwood.bin` with 321 entries (167 positions).
- **Syzygy tablebases** — WDL/DTZ probing for 6–7 man positions. Path configurable via UCI `SyzygyPath`.

## [0.2.0] - 2026-06-07

### Added
- **Search, evaluation, UCI (Phase 2)** — PVS with iterative deepening and aspiration windows, transposition table (1–1024 MB, depth-preferred replacement), move ordering (TT hash, MVV-LVA, killers, countermove, history), LMR, null-move pruning, futility pruning, razoring, quiescence search, check extension. Full UCI protocol support. `bench` and `selfplay` CLI subcommands. 12 new tests; 38/38 total.

## [0.1.0] - 2026-06-07

### Added
- **Foundation (Phase 1)** — 12-piece bitboard board, Zobrist hashing (incremental), FEN parse/output, full legal move generation (castling, en passant, all 4 promotion types), perft and perft-split, precomputed attack tables. CLI with `perft`, `fen`, `perftsuite`. 26 perft assertions across 6 positions, all pass.

[Unreleased]: https://github.com/akorite/driftwood/compare/v0.6.0...HEAD
[0.6.0]: https://github.com/akorite/driftwood/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/akorite/driftwood/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/akorite/driftwood/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/akorite/driftwood/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/akorite/driftwood/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/akorite/driftwood/releases/tag/v0.1.0
