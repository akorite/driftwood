#pragma once

#include <cstdint>
#include <string>

namespace driftwood {

class Board;

// ---------------------------------------------------------------------------
// Syzygy tablebase probe
//
// This module provides WDL (Win/Draw/Loss) and DTZ (Distance To Zeroing)
// probes for Syzygy format endgame tablebases.
//
// WDL values:  -2 = loss (but cursed), -1 = loss, 0 = draw,
//              +1 = win, +2 = win (but cursed)
// DTZ values: distance to zeroing of the best move (positive = winning,
//             negative = losing, 0 = draw)
//
// If tablebases are not configured or files are not present, all probe
// functions return false, and the engine falls back to evaluation.
// ---------------------------------------------------------------------------

// Check whether TB probing is available for the given position.
bool syzygy_available(const Board& board);

// Probe WDL (Win/Draw/Loss) for a position.
// Returns true if the probe succeeded, and `wdl` is set to -2/-1/0/+1/+2.
bool probe_wdl(const Board& board, int& wdl);

// Probe DTZ (Distance To Zeroing) for a position.
// Returns true if the probe succeeded, and `dtz` is set.
bool probe_dtz(const Board& board, int& dtz);

// Set the Syzygy tablebase directory path.
// An empty path disables tablebase probing.
void set_syzygy_path(const std::string& path);

// Get the current Syzygy path.
const std::string& get_syzygy_path();

// Initialize Syzygy tables. Must be called after set_syzygy_path().
// Returns true if initialization succeeded.
bool init_syzygy(const std::string& path);

// Maximum number of pieces for which tablebases are available (0 = unknown/not loaded)
int syzygy_max_pieces();

// Compute total number of pieces on the board.
int piece_count(const Board& board);

} // namespace driftwood
