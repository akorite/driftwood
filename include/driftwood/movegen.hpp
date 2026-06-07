#pragma once

#include "driftwood/types.hpp"
#include <cstdint>

namespace driftwood {

class Board;

// ---------------------------------------------------------------------------
// Attack tables (precomputed)
// ---------------------------------------------------------------------------

struct AttackTables {
    uint64_t knight[64];
    uint64_t king[64];
    uint64_t pawn[2][64]; // [color][square]
};

const AttackTables& get_attack_tables();

// ---------------------------------------------------------------------------
// Attack computation (useful for eval mobility counting)
// ---------------------------------------------------------------------------

uint64_t bishop_attacks(uint64_t occ, Square sq);
uint64_t rook_attacks(uint64_t occ, Square sq);
uint64_t queen_attacks(uint64_t occ, Square sq);

// ---------------------------------------------------------------------------
// Attack detection
// ---------------------------------------------------------------------------

bool is_square_attacked(const Board& board, Square sq, Color by_color);

// ---------------------------------------------------------------------------
// Move generation
// ---------------------------------------------------------------------------

void generate_pseudo_moves(const Board& board, MoveList& moves);
void generate_legal_moves(Board& board, MoveList& moves);
int count_legal_moves(Board& board);

} // namespace driftwood
