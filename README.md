# DriftWood

A chess engine built from scratch in C++17. Plays around 2300-2500 ELO, runs as a single binary, and has a web UI you can play against in your browser.

```
./driftwood serve
```

Open your browser, pick a side, and play.

---

## What it does

**The engine** — bitboard representation, hand-crafted evaluation, principal variation search with pruning, Lazy SMP multithreading, opening book, Syzygy tablebase support.

**The web UI** — drag-and-drop board with legal move dots, clock, move history, captured pieces, live eval. Dark theme. Everything vendored, no CDN, works offline.

**One binary** — handles UCI mode (for chess GUIs), CLI tools (perft, bench, selfplay), and the web server.

---

## Quick start

### Build it

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Needs a C++17 compiler (GCC 9+, Clang 10+) and CMake 3.16+. That's it.

### Play against it

```bash
./build/driftwood serve           # web UI on http://localhost:8080
./build/driftwood serve 9090      # custom port
```

<!-- SCREENSHOT: web-ui-main -->
![DriftWood Web UI](docs/screenshots/web-ui-main.png)

### Use it with a chess GUI

```bash
./build/driftwood                  # UCI mode (default)
echo "uci" | ./build/driftwood     # or pipe commands directly
```

Works with CuteChess, Banksia, or any UCI-compatible GUI.

---

## The web UI

<!-- SCREENSHOT: web-ui-gameplay -->
![Gameplay](docs/screenshots/web-ui-gameplay.png)

What you get:

- **Drag-and-drop** pieces with legal destination dots (filled = empty square, ring = capture)
- **Clock** — 1, 3, 10, or 30 minute presets
- **Move history** in SAN notation, clickable to jump back
- **Captured pieces** display with Unicode glyphs
- **Live evaluation** — updated every few seconds
- **Board flip**, keyboard shortcuts (`N` = new game, `F` = flip, `Esc` = close modal)
- **New Game** with white / black / random color choice
- **Resign** and **Draw** buttons

All third-party assets are vendored under `web/vendor/` — chessboard.js, chess.js, jQuery, and the cburnett piece set. No internet connection required.

---

## How the engine works

### Search

Principal Variation Search with iterative deepening and aspiration windows:

- **Late Move Reductions** — quiet moves late in the list get searched shallower
- **Null Move Pruning** — give the opponent a free move; if they still can't beat beta, prune
- **Futility Pruning** — at the horizon, skip quiet moves that can't reach alpha
- **Razoring** — at very low depth, if we're way below alpha, drop straight into quiescence
- **Internal Iterative Reduction** — no TT move? Search one ply less to save time
- **Late Move Pruning** — skip quiet moves beyond a move-count threshold at low depths
- **Check Extension** — always go one ply deeper when in check
- **Quiescence Search** — captures only, stand-pat, delta pruning

Move ordering: TT hash move → MVV-LVA captures → killers → countermove → history.

### Evaluation

Material + piece-square tables with midgame/endgame interpolation. Then positional terms:

- **Mobility** — how many squares each piece controls
- **King safety** — pawn shield, open files near the king, attacker/defender balance
- **Pawn structure** — doubled, isolated, passed, backward, candidate passers
- **Outposts** — knights on protected squares in enemy territory
- **Bishop pair** — bonus for having both bishops
- **Rook placement** — open files, semi-open files, 7th rank pressure
- **Space** — central file control by pawns and minor pieces
- **Threats** — hanging pieces, pressure on the enemy queen
- **Passed pawn kings** — king proximity to passed pawns in the endgame

### Threading

Lazy SMP — multiple threads share one transposition table and search independently. The TT is the only communication channel. Configure with `setoption name Threads value 4`.

### Opening book

321 entries covering common lines (Italian, Ruy Lopez, Sicilian, French, QGD, King's Indian). Weighted random selection for variety.

### Syzygy tablebases

WDL/DTZ probing for 6-7 man positions. Configure with `setoption name SyzygyPath value /path/to/tables`.

---

## CLI tools

```bash
./build/driftwood perft 5                    # count nodes at depth 5
./build/driftwood perft 4 <fen> --split      # per-move node counts
./build/driftwood perftsuite                 # run all perft positions
./build/driftwood bench 12                   # benchmark across 6 positions
./build/driftwood selfplay 20                # engine plays itself for 20 moves
./build/driftwood fen "rnbqkbnr/..."         # normalize a FEN string
```

---

## UCI options

| Option | Type | Default | Range | What it does |
|--------|------|---------|-------|--------------|
| `Hash` | spin | 64 | 1–1024 | TT size in MB |
| `Threads` | spin | 1 | 1–256 | Number of search threads |
| `SyzygyPath` | string | — | — | Path to Syzygy tablebase files |
| `BookFile` | string | books/driftwood.bin | — | Opening book path |
| `BookMoves` | spin | 12 | 0–100 | Max book moves to consider |

---

## HTTP API

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/new_game?color=white\|black\|random` | GET | Start a new game |
| `/api/move` | POST | Send a move, get engine's reply |
| `/api/state?fen=<FEN>` | GET | Get legal moves, check, mate status |
| `/api/eval?fen=<FEN>&depth=N` | GET | Engine evaluation with PV |

---

## Tests

```bash
cd build && ctest --output-on-failure
```

26 perft assertions, eval tests, TT tests, search tactic tests, Syzygy integration, Lazy SMP tests. All passing.

---

## Perft numbers

| Depth | Nodes |
|-------|-------|
| 1 | 20 |
| 2 | 400 |
| 3 | 8,902 |
| 4 | 197,281 |
| 5 | 4,865,609 |

---

## License

MIT. See [LICENSE](LICENSE).
