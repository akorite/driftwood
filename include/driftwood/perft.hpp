#pragma once

#include <cstdint>
#include <string>

namespace driftwood {

class Board;

// Perft (performance test) with bulk counting
uint64_t perft(Board& board, int depth);

// Perft split: shows per-move node counts
uint64_t perft_split(Board& board, int depth);

// Run the standard perftsuite
void run_perftsuite();

} // namespace driftwood
