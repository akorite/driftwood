#include "driftwood/eval.hpp"
#include "driftwood/board.hpp"
#include "driftwood/movegen.hpp"
#include <cstdint>
#include <algorithm>

namespace driftwood {

// ===========================================================================
// Evaluation tuning weights
//
// These weights are documented for future automated tuning (Texel tuning,
// etc.). All values are in centipawns unless noted.
//
// Material values:      Pawn=100, Knight=320, Bishop=330, Rook=500, Queen=900
// Phase weights:        Pawn=0, Knight=1, Bishop=1, Rook=2, Queen=4, King=0
// Max phase:            24
//
// Mobility weights (per piece, per attacked square in mobility area):
//   Knight: 4    Bishop: 5    Rook: 2    Queen: 1
//
// Pawn structure:
//   Doubled pawn penalty:          -10
//   Isolated pawn penalty:         -15
//   Backward pawn penalty:         -12
//   Candidate passer bonus:        +10
//   Passed pawn bonus (by rank):
//     rank 2: 10, 3: 15, 4: 30, 5: 60, 6: 120, 7: 200
//
// Bishop pair bonus (mg/eg):      30 / 50
//
// King safety:
//   Pawn shield bonus:             15 (near, same file)
//   Open file penalty near king:   -15
//   Half-open file penalty:        -8
//   Safe king zone attacker bonus: 3 per square of king zone attacked
//
// Knight outposts (mg/eg):
//   Rank 4: 15 / 20
//   Rank 5: 25 / 35
//   Rank 6: 40 / 55
//
// Rook on semi-open file:          10
// Rook on open file:               20
// Rook on 7th rank:                20
// ===========================================================================

// ---------------------------------------------------------------------------
// Material values
// ---------------------------------------------------------------------------

constexpr int MATERIAL[6] = {
    100,   // Pawn
    320,   // Knight
    330,   // Bishop
    500,   // Rook
    900,   // Queen
    0,     // King
};

// ---------------------------------------------------------------------------
// Piece-square tables (white's perspective, index = square 0-63)
// Layout: a1=0, b1=1, ..., h1=7, a2=8, ..., h8=63
// Black pieces use mirrored index: sq ^ 56 (vertical flip)
// ---------------------------------------------------------------------------

// Midgame PST
constexpr int MG_TABLE[6][64] = {
    // Pawns
    {
        0,   0,   0,   0,   0,   0,   0,   0,
       50,  50,  50,  50,  50,  50,  50,  50,
       10,  10,  20,  30,  30,  20,  10,  10,
        5,   5,  10,  25,  25,  10,   5,   5,
        0,   0,   0,  20,  20,   0,   0,   0,
        5,  -5, -10,   0,   0, -10,  -5,   5,
        5,  10,  10, -20, -20,  10,  10,   5,
        0,   0,   0,   0,   0,   0,   0,   0,
    },
    // Knights
    {
       -50, -40, -30, -30, -30, -30, -40, -50,
       -40, -20,   0,   0,   0,   0, -20, -40,
       -30,   0,  10,  15,  15,  10,   0, -30,
       -30,   5,  15,  20,  20,  15,   5, -30,
       -30,   0,  15,  20,  20,  15,   0, -30,
       -30,   5,  10,  15,  15,  10,   5, -30,
       -40, -20,   0,   5,   5,   0, -20, -40,
       -50, -40, -30, -30, -30, -30, -40, -50,
    },
    // Bishops
    {
       -20, -10, -10, -10, -10, -10, -10, -20,
       -10,   0,   0,   0,   0,   0,   0, -10,
       -10,   0,   5,  10,  10,   5,   0, -10,
       -10,   5,   5,  10,  10,   5,   5, -10,
       -10,   0,  10,  10,  10,  10,   0, -10,
       -10,  10,  10,  10,  10,  10,  10, -10,
       -10,   5,   0,   0,   0,   0,   5, -10,
       -20, -10, -10, -10, -10, -10, -10, -20,
    },
    // Rooks
    {
         0,   0,   0,   0,   0,   0,   0,   0,
         5,  10,  10,  10,  10,  10,  10,   5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
         0,   0,   0,   5,   5,   0,   0,   0,
    },
    // Queens
    {
       -20, -10, -10,  -5,  -5, -10, -10, -20,
       -10,   0,   0,   0,   0,   0,   0, -10,
       -10,   0,   5,   5,   5,   5,   0, -10,
        -5,   0,   5,   5,   5,   5,   0,  -5,
         0,   0,   5,   5,   5,   5,   0,  -5,
       -10,   5,   5,   5,   5,   5,   0, -10,
       -10,   0,   5,   0,   0,   0,   0, -10,
       -20, -10, -10,  -5,  -5, -10, -10, -20,
    },
    // Kings (midgame: pawn shelter near king is important)
    {
       -30, -40, -40, -50, -50, -40, -40, -30,
       -30, -40, -40, -50, -50, -40, -40, -30,
       -30, -40, -40, -50, -50, -40, -40, -30,
       -30, -40, -40, -50, -50, -40, -40, -30,
       -20, -30, -30, -40, -40, -30, -30, -20,
       -10, -20, -20, -20, -20, -20, -20, -10,
        20,  20,   0,   0,   0,   0,  20,  20,
        20,  30,  10,   0,   0,  10,  30,  20,
    },
};

// Endgame PST
constexpr int EG_TABLE[6][64] = {
    // Pawns
    {
        0,   0,   0,   0,   0,   0,   0,   0,
       80,  80,  80,  80,  80,  80,  80,  80,
       40,  40,  50,  60,  60,  50,  40,  40,
       20,  20,  40,  60,  60,  40,  20,  20,
       10,  10,  30,  50,  50,  30,  10,  10,
        5,   5,  20,  40,  40,  20,   5,   5,
        0,   0,  10,  20,  20,  10,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,
    },
    // Knights
    {
       -50, -40, -30, -30, -30, -30, -40, -50,
       -40, -20,   0,   0,   0,   0, -20, -40,
       -30,   0,  10,  15,  15,  10,   0, -30,
       -30,   5,  15,  20,  20,  15,   5, -30,
       -30,   0,  15,  20,  20,  15,   0, -30,
       -30,   5,  10,  15,  15,  10,   5, -30,
       -40, -20,   0,   5,   5,   0, -20, -40,
       -50, -40, -30, -30, -30, -30, -40, -50,
    },
    // Bishops
    {
       -20, -10, -10, -10, -10, -10, -10, -20,
       -10,   0,   0,   0,   0,   0,   0, -10,
       -10,   0,   5,  10,  10,   5,   0, -10,
       -10,   5,   5,  10,  10,   5,   5, -10,
       -10,   0,  10,  10,  10,  10,   0, -10,
       -10,  10,  10,  10,  10,  10,  10, -10,
       -10,   5,   0,   0,   0,   0,   5, -10,
       -20, -10, -10, -10, -10, -10, -10, -20,
    },
    // Rooks
    {
         0,   0,   0,   0,   0,   0,   0,   0,
         5,  10,  10,  10,  10,  10,  10,   5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
        -5,   0,   0,   0,   0,   0,   0,  -5,
         0,   0,   0,   5,   5,   0,   0,   0,
    },
    // Queens
    {
       -20, -10, -10,  -5,  -5, -10, -10, -20,
       -10,   0,   0,   0,   0,   0,   0, -10,
       -10,   0,   5,   5,   5,   5,   0, -10,
        -5,   0,   5,   5,   5,   5,   0,  -5,
         0,   0,   5,   5,   5,   5,   0,  -5,
       -10,   5,   5,   5,   5,   5,   0, -10,
       -10,   0,   5,   0,   0,   0,   0, -10,
       -20, -10, -10,  -5,  -5, -10, -10, -20,
    },
    // Kings (endgame: centralize!)
    {
       -50, -40, -30, -20, -20, -30, -40, -50,
       -30, -20, -10,   0,   0, -10, -20, -30,
       -30, -10,  20,  30,  30,  20, -10, -30,
       -30, -10,  30,  40,  40,  30, -10, -30,
       -30, -10,  30,  40,  40,  30, -10, -30,
       -30, -10,  20,  30,  30,  20, -10, -30,
       -30, -30,   0,   0,   0,   0, -30, -30,
       -50, -30, -30, -30, -30, -30, -30, -50,
    },
};

// Phase weights per piece type (index = piece type enum value)
constexpr int PHASE_WEIGHTS[6] = {0, 1, 1, 2, 4, 0};
constexpr int MAX_PHASE = 24;

// ---------------------------------------------------------------------------
// Helper: popcount for uint64_t (using builtin)
// ---------------------------------------------------------------------------

static inline int popcount(uint64_t x) {
    return __builtin_popcountll(x);
}

// ---------------------------------------------------------------------------
// Mobility
// ---------------------------------------------------------------------------

// Mobility weights per piece type (knight, bishop, rook, queen)
constexpr int MOBILITY_WEIGHT[4] = {4, 5, 2, 1};

// Back-rank penalty offset for rook mobility (discourage rooks on back rank)
constexpr int ROOK_BACK_RANK_PENALTY = -4;

static void evaluate_mobility(const Board& board, Color c,
                              int& mg_score, int& eg_score,
                              const AttackTables& att,
                              uint64_t occ, uint64_t us, uint64_t them)
{
    // Use sign so black contributions subtract correctly
    int sign = (c == Color::White) ? 1 : -1;

    // Mobility area: squares not occupied by own pieces (basic version)
    uint64_t area = ~us;

    // Knight mobility
    uint64_t knights = board.piece_bb(c, PieceType::Knight);
    while (knights) {
        int sq = __builtin_ctzll(knights);
        uint64_t attacks = att.knight[sq] & area;
        int count = popcount(attacks);
        mg_score += sign * count * MOBILITY_WEIGHT[0];
        eg_score += sign * count * MOBILITY_WEIGHT[0];
        knights &= knights - 1;
    }

    // Bishop mobility
    uint64_t bishops = board.piece_bb(c, PieceType::Bishop);
    while (bishops) {
        int sq = __builtin_ctzll(bishops);
        uint64_t attacks = bishop_attacks(occ, Square(static_cast<uint8_t>(sq))) & area;
        int count = popcount(attacks);
        mg_score += sign * count * MOBILITY_WEIGHT[1];
        eg_score += sign * count * MOBILITY_WEIGHT[1];
        bishops &= bishops - 1;
    }

    // Rook mobility — with back-rank penalty
    uint64_t rooks = board.piece_bb(c, PieceType::Rook);
    while (rooks) {
        int sq = __builtin_ctzll(rooks);
        uint64_t attacks = rook_attacks(occ, Square(static_cast<uint8_t>(sq))) & area;
        int count = popcount(attacks);
        mg_score += sign * count * MOBILITY_WEIGHT[2];
        eg_score += sign * count * MOBILITY_WEIGHT[2];
        // Penalty for rooks still on the back rank
        int r = sq / 8;
        if ((c == Color::White && r == 0) || (c == Color::Black && r == 7)) {
            mg_score += sign * ROOK_BACK_RANK_PENALTY;
            eg_score += sign * ROOK_BACK_RANK_PENALTY;
        }
        rooks &= rooks - 1;
    }

    // Queen mobility
    uint64_t queens = board.piece_bb(c, PieceType::Queen);
    while (queens) {
        int sq = __builtin_ctzll(queens);
        uint64_t attacks = queen_attacks(occ, Square(static_cast<uint8_t>(sq))) & area;
        int count = popcount(attacks);
        mg_score += sign * count * MOBILITY_WEIGHT[3];
        eg_score += sign * count * MOBILITY_WEIGHT[3];
        queens &= queens - 1;
    }
}

// ---------------------------------------------------------------------------
// Pawn structure
// ---------------------------------------------------------------------------

constexpr int DOUBLED_PAWN_PENALTY    = -10;
constexpr int ISOLATED_PAWN_PENALTY   = -15;
constexpr int BACKWARD_PAWN_PENALTY   = -12;
constexpr int CANDIDATE_PASSER_BONUS  =  10;

// Passed pawn bonus by rank (index 0-7, for white rank 2-7)
constexpr int PASSED_PAWN_BONUS[8] = {0, 0, 10, 15, 30, 60, 120, 200};

static void evaluate_pawn_structure(const Board& board, Color c,
                                    int& mg_score, int& eg_score,
                                    uint64_t our_pawns, uint64_t their_pawns,
                                    const AttackTables& att)
{
    int sign = (c == Color::White) ? 1 : -1;

    // File masks for each file
    constexpr uint64_t FILE_MASK[8] = {
        0x0101010101010101ULL, 0x0202020202020202ULL,
        0x0404040404040404ULL, 0x0808080808080808ULL,
        0x1010101010101010ULL, 0x2020202020202020ULL,
        0x4040404040404040ULL, 0x8080808080808080ULL,
    };

    // Count pawns per file
    int file_counts[8] = {0};
    uint64_t pawns = our_pawns;
    while (pawns) {
        int sq = __builtin_ctzll(pawns);
        int f = sq % 8;
        file_counts[f]++;
        pawns &= pawns - 1;
    }

    // Determine pawn files for quick lookup
    uint64_t pawn_files_bb = 0;
    for (int f = 0; f < 8; ++f) {
        if (file_counts[f] > 0) pawn_files_bb |= (1ULL << f);
    }

    // Evaluate each pawn
    pawns = our_pawns;
    while (pawns) {
        int sq = __builtin_ctzll(pawns);
        int r = sq / 8;
        int f = sq % 8;
        int mir_r = (c == Color::White) ? r : (7 - r); // relative rank 0-7

        // Doubled pawn check: more than 1 pawn on this file
        if (file_counts[f] >= 2) {
            mg_score += sign * DOUBLED_PAWN_PENALTY;
            eg_score += sign * DOUBLED_PAWN_PENALTY;
        }

        // Isolated pawn check: no friendly pawn on adjacent files
        bool isolated = true;
        if (f > 0 && file_counts[f-1] > 0) isolated = false;
        if (f < 7 && file_counts[f+1] > 0) isolated = false;
        if (isolated) {
            mg_score += sign * ISOLATED_PAWN_PENALTY;
            eg_score += sign * ISOLATED_PAWN_PENALTY;
        }

        // Passed pawn check: no enemy pawns on this or adjacent files ahead
        uint64_t ahead_mask = 0;
        if (c == Color::White) {
            for (int rr = r + 1; rr < 8; ++rr) {
                ahead_mask |= 1ULL << (rr * 8 + f);
                if (f > 0) ahead_mask |= 1ULL << (rr * 8 + (f - 1));
                if (f < 7) ahead_mask |= 1ULL << (rr * 8 + (f + 1));
            }
        } else {
            for (int rr = r - 1; rr >= 0; --rr) {
                ahead_mask |= 1ULL << (rr * 8 + f);
                if (f > 0) ahead_mask |= 1ULL << (rr * 8 + (f - 1));
                if (f < 7) ahead_mask |= 1ULL << (rr * 8 + (f + 1));
            }
        }

        bool is_passed = (their_pawns & ahead_mask) == 0;

        if (is_passed) {
            int bonus = PASSED_PAWN_BONUS[mir_r];
            mg_score += sign * bonus;
            eg_score += sign * bonus * 2;
        } else {
            // Check for backward pawn
            int advance_sq = (c == Color::White) ? sq + 8 : sq - 8;
            if (advance_sq >= 0 && advance_sq < 64) {
                uint64_t enemy_pawn_attacks_adv = att.pawn[to_int(opposite(c))][advance_sq];
                if (enemy_pawn_attacks_adv & their_pawns) {
                    // Can't safely advance. Check if supported by own pawns.
                    uint64_t support = att.pawn[to_int(c)][sq] & our_pawns;
                    if (!support) {
                        mg_score += sign * BACKWARD_PAWN_PENALTY;
                        eg_score += sign * BACKWARD_PAWN_PENALTY;
                    }

                    // Candidate passer: not passed, no direct enemy pawn in front
                    if (!(their_pawns & (1ULL << advance_sq))) {
                        int adj_blockers = 0;
                        for (int af = f - 1; af <= f + 1; af += 2) {
                            if (af >= 0 && af < 8) {
                                // Check if there's an enemy pawn on this adjacent file ahead
                                uint64_t enemy_on_file = their_pawns & FILE_MASK[af];
                                if (c == Color::White && enemy_on_file) {
                                    // Check if any are ahead
                                    while (enemy_on_file) {
                                        int es = __builtin_ctzll(enemy_on_file);
                                        if (es / 8 > r) { adj_blockers++; break; }
                                        enemy_on_file &= enemy_on_file - 1;
                                    }
                                } else if (c == Color::Black && enemy_on_file) {
                                    while (enemy_on_file) {
                                        int es = __builtin_ctzll(enemy_on_file);
                                        if (es / 8 < r) { adj_blockers++; break; }
                                        enemy_on_file &= enemy_on_file - 1;
                                    }
                                }
                            }
                        }
                        if (adj_blockers <= 1) {
                            mg_score += sign * CANDIDATE_PASSER_BONUS;
                            eg_score += sign * CANDIDATE_PASSER_BONUS;
                        }
                    }
                }
            }
        }

        pawns &= pawns - 1;
    }
}

// ---------------------------------------------------------------------------
// King safety
// ---------------------------------------------------------------------------

constexpr int PAWN_SHIELD_BONUS = 15;
constexpr int OPEN_FILE_PENALTY_NEAR_KING = -15;
constexpr int HALF_OPEN_FILE_PENALTY = -8;
constexpr int KING_ATTACK_WEIGHT_PER_ATTACKER = 3;

static void evaluate_king_safety(const Board& board, Color c,
                                 int& mg_score, int& eg_score,
                                 uint64_t our_pawns, uint64_t occ,
                                 uint64_t us, uint64_t them,
                                 const AttackTables& att)
{
    int sign = (c == Color::White) ? 1 : -1;
    int king_sq = board.find_king(c);
    if (king_sq < 0) return;

    int king_r = king_sq / 8;
    int king_f = king_sq % 8;

    // Pawn shield: friendly pawns on ranks 2/3 (white) or 7/6 (black)
    // in front of the king (same file + adjacent files)
    int shield_bonus = 0;
    for (int shield_f = king_f - 1; shield_f <= king_f + 1; ++shield_f) {
        if (shield_f < 0 || shield_f > 7) continue;
        if (c == Color::White) {
            if (our_pawns & (1ULL << (1*8 + shield_f))) shield_bonus += PAWN_SHIELD_BONUS;
            if (our_pawns & (1ULL << (2*8 + shield_f))) shield_bonus += PAWN_SHIELD_BONUS;
        } else {
            if (our_pawns & (1ULL << (6*8 + shield_f))) shield_bonus += PAWN_SHIELD_BONUS;
            if (our_pawns & (1ULL << (5*8 + shield_f))) shield_bonus += PAWN_SHIELD_BONUS;
        }
    }
    mg_score += sign * shield_bonus;

    // Open/half-open files near king
    for (int check_f = king_f - 1; check_f <= king_f + 1; ++check_f) {
        if (check_f < 0 || check_f > 7) continue;
        uint64_t file_pawns = our_pawns & (0x0101010101010101ULL << check_f);
        uint64_t enemy_file_pawns = board.piece_bb(opposite(c), PieceType::Pawn)
                                     & (0x0101010101010101ULL << check_f);
        if (file_pawns == 0) {
            if (enemy_file_pawns == 0) {
                // Open file near king
                mg_score += sign * OPEN_FILE_PENALTY_NEAR_KING;
            } else {
                // Half-open file near king (enemy pawns only)
                mg_score += sign * HALF_OPEN_FILE_PENALTY;
            }
        }
    }

    // King zone: the king's file, rank, and one square around
    uint64_t king_zone = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int df = -1; df <= 1; ++df) {
            int zr = king_r + dr;
            int zf = king_f + df;
            if (zr >= 0 && zr < 8 && zf >= 0 && zf < 8) {
                king_zone |= 1ULL << (zr * 8 + zf);
            }
        }
    }

    // Count enemy attackers near the king zone
    Color enemy = opposite(c);

    int attack_count = 0;

    // Knight attackers near king zone
    uint64_t knights = board.piece_bb(enemy, PieceType::Knight);
    while (knights) {
        int sq = __builtin_ctzll(knights);
        if (att.knight[sq] & king_zone) attack_count++;
        knights &= knights - 1;
    }

    // Bishop and queen diagonal attackers
    uint64_t bishops = board.piece_bb(enemy, PieceType::Bishop);
    while (bishops) {
        int sq = __builtin_ctzll(bishops);
        uint64_t diag = bishop_attacks(occ, Square(static_cast<uint8_t>(sq)));
        if (diag & king_zone) attack_count++;
        bishops &= bishops - 1;
    }

    // Rook and queen orthogonal attackers
    uint64_t rooks = board.piece_bb(enemy, PieceType::Rook);
    while (rooks) {
        int sq = __builtin_ctzll(rooks);
        uint64_t orth = rook_attacks(occ, Square(static_cast<uint8_t>(sq)));
        if (orth & king_zone) attack_count++;
        rooks &= rooks - 1;
    }

    // Queen attackers (already counted by both bishop and rook paths above — avoid double count)
    // We counted queens twice (in bishop and rook loops). Let's fix this:
    // Actually the bishop loop checks knight, bishop, so queen isn't in there.
    // Wait, I moved queen to the rook loop. Let me reconsider.
    // The code above:
    // - Knight loop: knights only
    // - Bishop loop: bishops only
    // - Rook loop: rooks only
    // Queen is not counted at all! Let me add queen.
    uint64_t queens = board.piece_bb(enemy, PieceType::Queen);
    while (queens) {
        int sq = __builtin_ctzll(queens);
        uint64_t all_att = queen_attacks(occ, Square(static_cast<uint8_t>(sq)));
        if (all_att & king_zone) attack_count++;
        queens &= queens - 1;
    }

    // Count friendly defenders near the king zone
    int defender_count = 0;
    uint64_t our_defenders = us & ~our_pawns & ~board.piece_bb(c, PieceType::King);
    while (our_defenders) {
        int sq = __builtin_ctzll(our_defenders);
        int zr = sq / 8, zf = sq % 8;
        if (std::abs(zr - king_r) <= 2 && std::abs(zf - king_f) <= 2) {
            defender_count++;
        }
        our_defenders &= our_defenders - 1;
    }

    int net_attackers = attack_count - defender_count;
    if (net_attackers > 0) {
        int attack_bonus = net_attackers * KING_ATTACK_WEIGHT_PER_ATTACKER * 8;
        // If we're evaluating for the opponent, this penalizes us (adds to mg_score)
        // If we're evaluating for ourselves, this helps the opponent (subtracts from mg_score)
        // So: sign is positive for white (we're evaluating white), negative for black
        // Attack bonus helps the opponent, so for white it's negative, for black it's positive
        // safety_score = -attack_bonus for white (worse for us), +attack_bonus for black (better for them)
        // Since we add to mg_score: + means good for us, - means bad for us
        // So: mg_score += opponent_attack_weight, where opponent_attack_weight is negative for us
        // Wait, I'm confusing myself. Let me think simply:
        // - If white has attackers near black king: good for white (+score for white)
        // - If black has attackers near white king: good for black (-score for white)
        // evaluate_king_safety(board, White): checks hazards near white king (bad for white)
        // evaluate_king_safety(board, Black): checks hazards near black king (good for white)
        //
        // In our evaluate, we call evaluate_king_safety(board, White, ...) and it adds to mg_score.
        // When we call it for White, we're looking at white's king safety.
        // If black attacks white's king zone, that's bad for white, so mg_score should decrease (negative).
        // If white defends, that reduces the penalty.
        // So for White evaluation: net_attackers = enemy_attackers - our_defenders (positive = bad for white = negative score)
        // mg_score += sign * (-net_attackers * weight) where sign = +1 for white
        // mg_score += -net_attackers * weight for white
        // mg_score += +net_attackers * weight for black (since sign = -1, -1 * -net = +net)
        // Hmm, let me simplify:
        // net_attackers_positive_benefits_opponent = attack_count - defender_count
        // For white: this is bad, so subtract from score
        // For black: this is bad for black, so add to score (since black subtracts)
        // Using sign: mg_score += sign * (-net_attackers * weight)
        // sign=+1 for white: mg_score += -net_attackers * weight (bad for white)
        // sign=-1 for black: mg_score += +net_attackers * weight (bad for black = good for white)
        int safety_penalty = net_attackers * KING_ATTACK_WEIGHT_PER_ATTACKER * 8;
        mg_score += sign * (-safety_penalty);
        eg_score += sign * (-safety_penalty / 2);
    }
}

// ---------------------------------------------------------------------------
// Outposts (knights)
// ---------------------------------------------------------------------------

constexpr int OUTPOST_BONUS_MG[3] = {15, 25, 40};
constexpr int OUTPOST_BONUS_EG[3] = {20, 35, 55};

static void evaluate_outposts(const Board& board, Color c,
                              int& mg_score, int& eg_score,
                              uint64_t our_pawns, uint64_t their_pawns,
                              const AttackTables& att)
{
    int sign = (c == Color::White) ? 1 : -1;
    uint64_t knights = board.piece_bb(c, PieceType::Knight);
    while (knights) {
        int sq = __builtin_ctzll(knights);
        int r = sq / 8;
        int mir_r = (c == Color::White) ? r : (7 - r); // relative rank 0-7

        // Outpost must be on rank 4, 5, or 6 (relative ranks 3, 4, 5)
        if (mir_r < 3 || mir_r > 5) {
            knights &= knights - 1;
            continue;
        }

        // Check if defended by a friendly pawn
        // att.pawn[opposite(c)][sq] gives squares of pawns that attack sq
        uint64_t pawn_attackers = att.pawn[to_int(opposite(c))][sq];
        bool defended = (pawn_attackers & our_pawns) != 0;
        if (!defended) {
            knights &= knights - 1;
            continue;
        }

        // Check if attackable by enemy pawns
        // Enemy pawns attack this square from sq+7, sq+9 (for knights attacked from below)
        // or sq-7, sq-9 (for knights attacked from above)
        uint64_t enemy_pawn_positions = 0;
        if (c == Color::White) {
            // Enemy = black, attacks downward
            if (sq + 7 < 64 && (sq + 7) % 8 != 7) enemy_pawn_positions |= 1ULL << (sq + 7);
            if (sq + 9 < 64 && (sq + 9) % 8 != 0) enemy_pawn_positions |= 1ULL << (sq + 9);
        } else {
            // Enemy = white, attacks upward
            if (sq - 7 >= 0 && (sq - 7) % 8 != 0) enemy_pawn_positions |= 1ULL << (sq - 7);
            if (sq - 9 >= 0 && (sq - 9) % 8 != 7) enemy_pawn_positions |= 1ULL << (sq - 9);
        }

        if (enemy_pawn_positions & their_pawns) {
            knights &= knights - 1;
            continue;
        }

        // It's an outpost!
        int idx = mir_r - 3; // 0, 1, 2 for ranks 4, 5, 6
        mg_score += sign * OUTPOST_BONUS_MG[idx];
        eg_score += sign * OUTPOST_BONUS_EG[idx];

        knights &= knights - 1;
    }
}

// ---------------------------------------------------------------------------
// Bishop pair
// ---------------------------------------------------------------------------

constexpr int BISHOP_PAIR_MG = 30;
constexpr int BISHOP_PAIR_EG = 50;

static void evaluate_bishop_pair(const Board& board, Color c,
                                 int& mg_score, int& eg_score)
{
    int sign = (c == Color::White) ? 1 : -1;
    if (popcount(board.piece_bb(c, PieceType::Bishop)) >= 2) {
        mg_score += sign * BISHOP_PAIR_MG;
        eg_score += sign * BISHOP_PAIR_EG;
    }
}

// ---------------------------------------------------------------------------
// Rook on open / semi-open files and 7th rank
// ---------------------------------------------------------------------------

constexpr int ROOK_SEMI_OPEN_FILE = 10;
constexpr int ROOK_OPEN_FILE = 20;
constexpr int ROOK_ON_SEVENTH = 20;

static void evaluate_rook_placement(const Board& board, Color c,
                                    int& mg_score, int& eg_score,
                                    uint64_t our_pawns, uint64_t their_pawns)
{
    int sign = (c == Color::White) ? 1 : -1;
    uint64_t rooks = board.piece_bb(c, PieceType::Rook);
    while (rooks) {
        int sq = __builtin_ctzll(rooks);
        int f = sq % 8;
        uint64_t file_mask = 0x0101010101010101ULL << f;

        bool has_our_pawn = (our_pawns & file_mask) != 0;
        bool has_their_pawn = (their_pawns & file_mask) != 0;

        if (!has_our_pawn && !has_their_pawn) {
            mg_score += sign * ROOK_OPEN_FILE;
            eg_score += sign * ROOK_OPEN_FILE;
        } else if (!has_our_pawn && has_their_pawn) {
            mg_score += sign * ROOK_SEMI_OPEN_FILE;
            eg_score += sign * ROOK_SEMI_OPEN_FILE;
        }

        // Rook on 7th rank (or 2nd for black)
        int r = sq / 8;
        if ((c == Color::White && r == 6) || (c == Color::Black && r == 1)) {
            mg_score += sign * ROOK_ON_SEVENTH;
            eg_score += sign * ROOK_ON_SEVENTH;
        }

        rooks &= rooks - 1;
    }
}

// ---------------------------------------------------------------------------
// Main evaluation
// ---------------------------------------------------------------------------

int evaluate(const Board& board) {
    int mg_score = 0;
    int eg_score = 0;
    int phase = 0;

    const auto& att = get_attack_tables();
    uint64_t occ = board.all_pieces();
    uint64_t white_pieces = board.by_color(Color::White);
    uint64_t black_pieces = board.by_color(Color::Black);

    // ---------- Material + PST (base) ----------
    for (int pt = 0; pt < 6; ++pt) {
        PieceType ptype = static_cast<PieceType>(pt);

        uint64_t wb = board.piece_bb(Color::White, ptype);
        while (wb) {
            int sq = __builtin_ctzll(wb);
            mg_score += MATERIAL[pt] + MG_TABLE[pt][sq];
            eg_score += MATERIAL[pt] + EG_TABLE[pt][sq];
            phase += PHASE_WEIGHTS[pt];
            wb &= wb - 1;
        }

        uint64_t bb = board.piece_bb(Color::Black, ptype);
        while (bb) {
            int sq = __builtin_ctzll(bb);
            int mir = sq ^ 56;
            mg_score -= MATERIAL[pt] + MG_TABLE[pt][mir];
            eg_score -= MATERIAL[pt] + EG_TABLE[pt][mir];
            phase += PHASE_WEIGHTS[pt];
            bb &= bb - 1;
        }
    }

    if (phase > MAX_PHASE) phase = MAX_PHASE;

    // ---------- Positional terms ----------
    uint64_t w_pawns = board.piece_bb(Color::White, PieceType::Pawn);
    uint64_t b_pawns = board.piece_bb(Color::Black, PieceType::Pawn);

    // White positional terms (add to score)
    evaluate_mobility(board, Color::White, mg_score, eg_score,
                      att, occ, white_pieces, black_pieces);
    evaluate_pawn_structure(board, Color::White, mg_score, eg_score,
                            w_pawns, b_pawns, att);
    evaluate_king_safety(board, Color::White, mg_score, eg_score,
                         w_pawns, occ, white_pieces, black_pieces, att);
    evaluate_outposts(board, Color::White, mg_score, eg_score,
                      w_pawns, b_pawns, att);
    evaluate_bishop_pair(board, Color::White, mg_score, eg_score);
    evaluate_rook_placement(board, Color::White, mg_score, eg_score,
                            w_pawns, b_pawns);

    // Black positional terms (subtract from score — handled internally via sign)
    evaluate_mobility(board, Color::Black, mg_score, eg_score,
                      att, occ, black_pieces, white_pieces);
    evaluate_pawn_structure(board, Color::Black, mg_score, eg_score,
                            b_pawns, w_pawns, att);
    evaluate_king_safety(board, Color::Black, mg_score, eg_score,
                         b_pawns, occ, black_pieces, white_pieces, att);
    evaluate_outposts(board, Color::Black, mg_score, eg_score,
                      b_pawns, w_pawns, att);
    evaluate_bishop_pair(board, Color::Black, mg_score, eg_score);
    evaluate_rook_placement(board, Color::Black, mg_score, eg_score,
                            b_pawns, w_pawns);

    // Phase interpolation
    int score = (mg_score * phase + eg_score * (MAX_PHASE - phase)) / MAX_PHASE;

    // Side-to-move perspective
    if (board.side_to_move() == Color::Black) {
        score = -score;
    }

    // Tempo bonus
    score += 10;

    return score;
}

} // namespace driftwood
