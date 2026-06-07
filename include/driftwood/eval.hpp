#pragma once

namespace driftwood {

class Board;

// Evaluate the board from the side-to-move's perspective.
// Positive scores favour the side to move.
int evaluate(const Board& board);

} // namespace driftwood
