# Architecture

This document describes the internal architecture of DriftWood for contributors.
It complements the README, which is aimed at users.

## High-level overview

```
                  ┌─────────────────────────────────────┐
                  │   Web UI (chessboard.js + chess.js) │
                  │   web/index.html, web/app.js        │
                  └──────────────┬──────────────────────┘
                                 │ HTTP /api/{new_game,move,state,eval}
                  ┌──────────────▼──────────────────────┐
                  │   Embedded HTTP server              │
                  │   src/serve.cpp (cpp-httplib)       │
                  └──────────────┬──────────────────────┘
                                 │ Searcher API
                  ┌──────────────▼──────────────────────┐
                  │   Searcher (PVS, Lazy SMP, time mgmt)│
                  │   src/search.cpp                     │
                  └──┬────────┬────────┬────────┬────────┘
                     │        │        │        │
            ┌────────▼─┐ ┌────▼────┐ ┌─▼──────┐ ┌▼─────────┐
            │  Eval    │ │   TT    │ │  Book  │ │  Syzygy  │
            │ material │ │ Zobrist │ │ weight │ │ WDL/DTZ  │
            │ + PST    │ │ depth-  │ │ random │ │ 6-7 man  │
            │ + phase  │ │ prefer  │ │        │ │          │
            └────────┬─┘ └────┬────┘ └────────┘ └──────────┘
                     │        │
            ┌────────▼────────▼────┐
            │  Board (bitboards)   │
            │  Zobrist, FEN        │
            │  make/unmake         │
            └────────┬─────────────┘
                     │
            ┌────────▼────────┐
            │   Move gen      │
            │  magic bitboard │
            │  attacks        │
            └─────────────────┘
```

The engine has a single binary that runs in three modes:

1. **UCI mode** (`driftwood` with no args) — speaks the Universal Chess Interface protocol on stdin/stdout for use with any UCI frontend (CuteChess, Banksia, etc.).
2. **CLI mode** (`driftwood perft`, `bench`, `selfplay`, `fen`, `perftsuite`) — direct engine introspection for development.
3. **Serve mode** (`driftwood serve [port]`) — starts the embedded HTTP server, serves the web UI, and plays games in the browser.

The shared `Searcher` is used by all three modes. The HTTP server is just a thin shim that converts REST calls into `Searcher` invocations.

## Board representation

The board uses 12 bitboards (`uint64_t`), one per piece type × color. Helper macros in `include/driftwood/types.hpp` map piece → bitboard.

- **Pros**: SIMD-friendly, fast bitwise operations, dense (96 bytes for the whole position).
- **Cons**: Slow for "is square X attacked by pawn of color C?" — needs a per-square attack table or magic bitboards. We precompute attack tables in `movegen.cpp` (knight, king, pawn) and use rotated bitboards / magic bitboards for sliders.

`make_move` / `unmake_move` are constant-time in the number of pieces involved (~10 cycles). We do **not** copy the board on every search node.

### Zobrist hashing

`include/driftwood/board.hpp:zobrist_init()` precomputes 12 × 64 random 64-bit keys for `[piece][square]`, plus keys for side-to-move, castling rights, and en-passant file. Hash is updated incrementally on every `make_move` and `unmake_move` — we never recompute it from scratch in the hot path.

## Move generation

`include/driftwood/movegen.hpp:generate_legal_moves()` and `generate_pseudo_legal_moves()`. Pseudo-legal is faster but includes moves that leave the king in check; legal generation adds a `was_in_check` filter for the side to move.

- **Pawns**: precomputed attack tables indexed by `[color][square]`.
- **Knights / Kings**: precomputed jump tables.
- **Bishops / Rooks / Queens**: magic bitboard tables (4.8 MB total). Bitboards are extracted from occupancy, indexed into a 64K-entry attack table, and the attack set is returned as a `uint64_t`.

## Search

`src/search.cpp` implements Principal Variation Search with iterative deepening:

```
for depth in 1..max_depth:
    score, pv = pvs(depth, alpha=-∞, beta=+∞)
    if score <= alpha or score >= beta:
        # Aspiration window fail
        score, pv = pvs(depth, alpha=-∞, beta=+∞)  # full window
    report(info depth, score, pv, nps)
    if time_up: break
```

### Pruning and reductions

- **Late-move reductions (LMR)**: moves late in the ordering are searched at `depth - reduction`. Reduction table is hand-tuned.
- **Null-move pruning**: at non-endgame positions, give the opponent a free move and search at reduced depth. Re-search at full depth if it fails high.
- **Futility pruning**: at the frontier (depth ≤ 3), prune quiet moves if static eval + margin can't reach beta.
- **Razoring**: at very low depth, if static eval + large margin < alpha, drop directly into quiescence.
- **Check extension**: extend by 1 ply when the side to move is in check.

### Move ordering

Order in `search.cpp::score_move()`:
1. TT hash move (highest priority, single best from a previous search)
2. Captures: MVV-LVA (most-valuable victim, least-valuable attacker)
3. Killer moves (2 per ply, quiets that caused a beta cutoff)
4. Countermove heuristic (the move that refuted the opponent's last move in this subtree)
5. History heuristic (cumulative success count, with decay)

### Quiescence search

`search.cpp::qsearch()`: stand-pat, then generate captures only and search them. MVV-LVA capture ordering is reused. Delta pruning: if `stand_pat + 1000 + see_gain < alpha`, prune.

### Lazy SMP

Multiple threads share a single transposition table. Each thread:
- Has its own `ThreadContext` (history, killers, countermove, search stack, node counter)
- Staggers its starting depth (thread 0 starts at 1, others at 1 or 2) to avoid lockstep
- Reads/writes the shared TT under a per-bucket mutex

The TT is the sole communication channel — when one thread finds a beta cutoff, it stores the result; other threads pick it up as the TT hash move.

### Time management

Casual-play tuning (see `src/search.cpp:search_worker` and `Searcher::search`):

| Clock | Budget formula | Cap |
|-------|----------------|-----|
| > 60s | `wtime/50 + winc/2` | 2s |
| 10–60s | `wtime/40 + winc/2` | 2s |
| 2–10s | `wtime/20 + winc/2` | 2s |
| < 2s | `wtime/10 + winc/2` | 2s |

We also cap `max_depth` per budget tier so a single deep iteration can't blow past the wall-clock budget (search granularity is per-depth, not per-node, so even a 2s cap can be exceeded by 3–5x without this guard):

| Budget | max_depth |
|--------|-----------|
| ≤ 200ms | 7 |
| ≤ 500ms | 8 |
| ≤ 1s | 9 |
| ≤ 2s | 11 |
| > 2s | 64 (default) |

`time_up()` is checked at every depth boundary and **immediately after every PVS call** so a long iteration bails without storing a worse-than-expected score.

## Evaluation

`src/eval.cpp`:

- **Material**: P=100, N=320, B=330, R=500, Q=900 (centipawns)
- **Piece-square tables**: 6 tables per piece per phase × 64 squares
- **Phase interpolation**: 24 (max) → 0 (endgame), weighted blend
- **Positional bonuses** (Phase 3):
  - Mobility: N, B, R, Q
  - King safety: pawn shield, open files near king, attacker weights
  - Pawn structure: doubled, isolated, passed, backward, candidate passers
  - Outposts (knights on protected squares in enemy half)
  - Bishop pair

Tempo bonus for the side to move.

## Opening book

`src/book.cpp` reads a binary file of `BookEntry { uint64_t zobrist, uint16_t move, uint16_t weight, uint16_t padding }`. The default `books/driftwood.bin` contains 321 entries (167 positions) covering common Italian, Ruy Lopez, Sicilian, French, Queen's Gambit, and King's Indian lines.

`probe_book()` does a linear scan (binary content is small). For 100K+ positions, swap to a hash table.

## Syzygy tablebases

`src/syzygy.cpp` probes 6- and 7-man WDL/DTZ files at configured `SyzygyPath`. Used only at the root — qsearch doesn't probe (to avoid complexity). The probe returns `WIN/DRAW/LOSS` and a DTZ distance; we map that to a score and pick the move that minimizes distance to conversion.

## HTTP server

`src/serve.cpp` uses cpp-httplib (vendored at `third_party/httplib.h`).

Endpoints:

| Method | Path | Purpose |
|--------|------|---------|
| GET  | `/api/new_game?color=&time=` | Start a new game; returns FEN + game ID |
| POST | `/api/move` | Send a move; engine replies with its reply |
| GET  | `/api/state?fen=` | Inspect position (legal moves, check, mate) |
| GET  | `/api/eval?fen=&depth=` | Static evaluation with PV |
| GET  | `/vendor/*`, `/` | Static files (UI) |

The server holds a single mutex around the board state — one game at a time. Multi-game is a TODO.

## Tests

- `tests/perft_test.cpp` — 26 perft assertions across 6 positions (startpos, Kiwipete, etc.)
- `tests/engine_test.cpp` — 50+ tests for eval, TT, search tactics, Lazy SMP determinism

Run with `ctest` from the build directory.

## Coding conventions

- C++17 only — no `std::format`, no modules, no concepts
- Public API in `include/driftwood/*.hpp` (header-only when practical)
- Zero heap allocation in hot paths (search, move gen)
- No exceptions in hot paths
- No raw `new` / `delete` — use `std::unique_ptr` if needed
- snake_case for functions, CamelCase for types, UPPER_SNAKE for constants
- `static` for all functions not exported across translation units
- Comment the *why*, not the *what*
