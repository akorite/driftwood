#include "driftwood/movegen.hpp"
#include "driftwood/board.hpp"
#include <cstdint>

namespace driftwood {

// ---------------------------------------------------------------------------
// Precomputed attack tables
// ---------------------------------------------------------------------------

const AttackTables& get_attack_tables() {
    static const AttackTables tables = []{
        AttackTables t{};
        for (int sq = 0; sq < 64; sq++) {
            uint8_t r = static_cast<uint8_t>(sq / 8);
            uint8_t f = static_cast<uint8_t>(sq % 8);

            // Knight attacks
            uint64_t knight_bb = 0;
            constexpr int knight_offsets[8][2] = {
                {2,1},{2,-1},{-2,1},{-2,-1},{1,2},{1,-2},{-1,2},{-1,-2}
            };
            for (auto& d : knight_offsets) {
                int nr = static_cast<int>(r) + d[0];
                int nf = static_cast<int>(f) + d[1];
                if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                    knight_bb |= 1ULL << (nr * 8 + nf);
                }
            }
            t.knight[sq] = knight_bb;

            // King attacks
            uint64_t king_bb = 0;
            constexpr int king_offsets[8][2] = {
                {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}
            };
            for (auto& d : king_offsets) {
                int nr = static_cast<int>(r) + d[0];
                int nf = static_cast<int>(f) + d[1];
                if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                    king_bb |= 1ULL << (nr * 8 + nf);
                }
            }
            t.king[sq] = king_bb;

            // White pawn attacks (attack upward: NE and NW)
            uint64_t wp_bb = 0;
            if (r < 7) {
                if (f > 0)   wp_bb |= 1ULL << ((r + 1) * 8 + (f - 1));
                if (f < 7)   wp_bb |= 1ULL << ((r + 1) * 8 + (f + 1));
            }
            t.pawn[0][sq] = wp_bb;

            // Black pawn attacks (attack downward: SE and SW)
            uint64_t bp_bb = 0;
            if (r > 0) {
                if (f > 0)   bp_bb |= 1ULL << ((r - 1) * 8 + (f - 1));
                if (f < 7)   bp_bb |= 1ULL << ((r - 1) * 8 + (f + 1));
            }
            t.pawn[1][sq] = bp_bb;
        }
        return t;
    }();
    return tables;
}

// ---------------------------------------------------------------------------
// Sliding attack computation
// ---------------------------------------------------------------------------

uint64_t rook_attacks(uint64_t occ, Square sq) {
    uint8_t r = sq.rank();
    uint8_t f = sq.file();
    uint64_t attacks = 0;

    // North
    for (int nr = static_cast<int>(r) + 1; nr < 8; nr++) {
        uint8_t idx = static_cast<uint8_t>(nr * 8 + f);
        attacks |= 1ULL << idx;
        if (occ & (1ULL << idx)) break;
    }
    // South
    for (int nr = static_cast<int>(r) - 1; nr >= 0; nr--) {
        uint8_t idx = static_cast<uint8_t>(nr * 8 + f);
        attacks |= 1ULL << idx;
        if (occ & (1ULL << idx)) break;
    }
    // East
    for (int nf = static_cast<int>(f) + 1; nf < 8; nf++) {
        uint8_t idx = static_cast<uint8_t>(r * 8 + nf);
        attacks |= 1ULL << idx;
        if (occ & (1ULL << idx)) break;
    }
    // West
    for (int nf = static_cast<int>(f) - 1; nf >= 0; nf--) {
        uint8_t idx = static_cast<uint8_t>(r * 8 + nf);
        attacks |= 1ULL << idx;
        if (occ & (1ULL << idx)) break;
    }

    return attacks;
}

uint64_t bishop_attacks(uint64_t occ, Square sq) {
    uint8_t r = sq.rank();
    uint8_t f = sq.file();
    uint64_t attacks = 0;

    // NE
    for (int d = 1; static_cast<int>(r) + d < 8 && static_cast<int>(f) + d < 8; d++) {
        uint8_t idx = static_cast<uint8_t>((r + d) * 8 + (f + d));
        attacks |= 1ULL << idx;
        if (occ & (1ULL << idx)) break;
    }
    // NW
    for (int d = 1; static_cast<int>(r) + d < 8 && static_cast<int>(f) - d >= 0; d++) {
        uint8_t idx = static_cast<uint8_t>((r + d) * 8 + (f - d));
        attacks |= 1ULL << idx;
        if (occ & (1ULL << idx)) break;
    }
    // SE
    for (int d = 1; static_cast<int>(r) - d >= 0 && static_cast<int>(f) + d < 8; d++) {
        uint8_t idx = static_cast<uint8_t>((r - d) * 8 + (f + d));
        attacks |= 1ULL << idx;
        if (occ & (1ULL << idx)) break;
    }
    // SW
    for (int d = 1; static_cast<int>(r) - d >= 0 && static_cast<int>(f) - d >= 0; d++) {
        uint8_t idx = static_cast<uint8_t>((r - d) * 8 + (f - d));
        attacks |= 1ULL << idx;
        if (occ & (1ULL << idx)) break;
    }

    return attacks;
}

uint64_t queen_attacks(uint64_t occ, Square sq) {
    return rook_attacks(occ, sq) | bishop_attacks(occ, sq);
}

// ---------------------------------------------------------------------------
// Attack detection
// ---------------------------------------------------------------------------

bool is_square_attacked(const Board& board, Square sq, Color by_color) {
    uint64_t occ = board.all_pieces();
    const uint64_t* by_type = board.pieces_for(by_color);
    const auto& att = get_attack_tables();

    // Pawn attacks: a pawn of color `by_color` attacks `sq` if the pawn
    // is on a square given by PAWN_ATTACKS[opposite(by_color)][sq].
    Color opp = opposite(by_color);
    if (att.pawn[to_int(opp)][sq.index] & by_type[static_cast<int>(PieceType::Pawn)]) {
        return true;
    }

    // Knight attacks
    if (att.knight[sq.index] & by_type[static_cast<int>(PieceType::Knight)]) {
        return true;
    }

    // King attacks
    if (att.king[sq.index] & by_type[static_cast<int>(PieceType::King)]) {
        return true;
    }

    // Bishop/Queen diagonal attacks
    uint64_t diag = bishop_attacks(occ, sq);
    if (diag & (by_type[static_cast<int>(PieceType::Bishop)] | by_type[static_cast<int>(PieceType::Queen)])) {
        return true;
    }

    // Rook/Queen orthogonal attacks
    uint64_t orth = rook_attacks(occ, sq);
    if (orth & (by_type[static_cast<int>(PieceType::Rook)] | by_type[static_cast<int>(PieceType::Queen)])) {
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Pseudo-legal move generation
// ---------------------------------------------------------------------------

static void gen_pawn_moves(const Board& board, Color side, Color /*opp*/,
                           uint64_t /*us*/, uint64_t them, uint64_t empty,
                           const AttackTables& att, MoveList& moves)
{
    uint64_t pawns = board.piece_bb(side, PieceType::Pawn);
    uint8_t double_rank = (side == Color::White) ? 1 : 6;
    int push_dir = (side == Color::White) ? 8 : -8;
    uint8_t promotes_from_rank = (side == Color::White) ? 6 : 1;

    while (pawns) {
        uint8_t sq_idx = static_cast<uint8_t>(__builtin_ctzll(pawns));
        Square sq(sq_idx);
        uint8_t r = sq.rank();

        // Single push
        int push_sq = static_cast<int>(sq_idx) + push_dir;
        if (push_sq >= 0 && push_sq < 64 && (empty & (1ULL << push_sq))) {
            if (r == promotes_from_rank) {
                // Promotion (single push)
                for (auto promo_pt : PROMOTION_TYPES) {
                    moves.add(Move::make(sq, Square(static_cast<uint8_t>(push_sq)),
                                         PieceType::Pawn, -1, static_cast<int>(promo_pt), MoveFlags::Normal));
                }
            } else {
                // Non-promotion single push
                moves.add(Move::make(sq, Square(static_cast<uint8_t>(push_sq)),
                                     PieceType::Pawn, -1, -1, MoveFlags::Normal));
                // Double push from starting rank
                if (r == double_rank) {
                    int dbl_sq = static_cast<int>(sq_idx) + 2 * push_dir;
                    if (dbl_sq >= 0 && dbl_sq < 64 && (empty & (1ULL << dbl_sq))) {
                        moves.add(Move::make(sq, Square(static_cast<uint8_t>(dbl_sq)),
                                             PieceType::Pawn, -1, -1, MoveFlags::DoublePawnPush));
                    }
                }
            }
        }

        // Captures (including en passant and promotion captures)
        uint64_t pawn_attacks = att.pawn[to_int(side)][sq_idx] & them;
        while (pawn_attacks) {
            uint8_t to_idx = static_cast<uint8_t>(__builtin_ctzll(pawn_attacks));
            Square to(to_idx);
            Color cap_c;
            PieceType cap_pt;
            board.piece_at(to, cap_c, cap_pt);
            int captured = static_cast<int>(cap_pt);
            if (r == promotes_from_rank) {
                // Promotion capture
                for (auto promo_pt : PROMOTION_TYPES) {
                    moves.add(Move::make(sq, to, PieceType::Pawn, captured,
                                         static_cast<int>(promo_pt), MoveFlags::Normal));
                }
            } else {
                moves.add(Move::make(sq, to, PieceType::Pawn, captured, -1, MoveFlags::Normal));
            }
            pawn_attacks &= pawn_attacks - 1;
        }

        // En passant
        int ep = board.ep_square();
        if (ep >= 0) {
            uint8_t ep_rank = (side == Color::White) ? 4 : 3;
            if (r == ep_rank) {
                uint64_t ep_attacks = att.pawn[to_int(side)][sq_idx] & (1ULL << ep);
                if (ep_attacks) {
                    Square ep_sq(static_cast<uint8_t>(ep));
                    int cap_sq = (side == Color::White) ? ep - 8 : ep + 8;
                    Color cap_c;
                    PieceType cap_pt;
                    board.piece_at(Square(static_cast<uint8_t>(cap_sq)), cap_c, cap_pt);
                    moves.add(Move::make(sq, ep_sq, PieceType::Pawn,
                                         static_cast<int>(cap_pt), -1, MoveFlags::EnPassant));
                }
            }
        }

        pawns &= pawns - 1;
    }
}

static void gen_slider_moves(const Board& board, Color side, Color /*opp*/,
                             uint64_t us, uint64_t them,
                             PieceType pt,
                             uint64_t (*attacks_fn)(uint64_t, Square),
                             MoveList& moves)
{
    uint64_t pieces = board.piece_bb(side, pt);
    while (pieces) {
        uint8_t sq_idx = static_cast<uint8_t>(__builtin_ctzll(pieces));
        Square sq(sq_idx);
        uint64_t targets = attacks_fn(board.all_pieces(), sq) & ~us;
        while (targets) {
            uint8_t to_idx = static_cast<uint8_t>(__builtin_ctzll(targets));
            Square to(to_idx);
            int captured = (them & (1ULL << to_idx)) ? [&]{
                Color cap_c;
                PieceType cap_pt;
                board.piece_at(to, cap_c, cap_pt);
                return static_cast<int>(cap_pt);
            }() : -1;
            moves.add(Move::make(sq, to, pt, captured, -1, MoveFlags::Normal));
            targets &= targets - 1;
        }
        pieces &= pieces - 1;
    }
}

static void gen_king_moves(const Board& board, Color side, Color /*opp*/,
                           uint64_t us, uint64_t them,
                           const AttackTables& att, MoveList& moves)
{
    uint64_t kings = board.piece_bb(side, PieceType::King);
    if (kings == 0) return;
    uint8_t sq_idx = static_cast<uint8_t>(__builtin_ctzll(kings));
    Square sq(sq_idx);
    uint64_t targets = att.king[sq_idx] & ~us;
    while (targets) {
        uint8_t to_idx = static_cast<uint8_t>(__builtin_ctzll(targets));
        Square to(to_idx);
        int captured = (them & (1ULL << to_idx)) ? [&]{
            Color cap_c;
            PieceType cap_pt;
            board.piece_at(to, cap_c, cap_pt);
            return static_cast<int>(cap_pt);
        }() : -1;
        moves.add(Move::make(sq, to, PieceType::King, captured, -1, MoveFlags::Normal));
        targets &= targets - 1;
    }
}

static void gen_castling_moves(const Board& board, Color side, Color /*opp*/,
                               uint64_t occ, MoveList& moves)
{
    uint8_t cr = board.castling_rights();
    uint64_t kings = board.piece_bb(side, PieceType::King);
    if (kings == 0) return;
    uint8_t king_idx = static_cast<uint8_t>(__builtin_ctzll(kings));
    uint8_t king_file = king_idx % 8;
    uint8_t king_rank = king_idx / 8;

    if (king_file != 4 || (king_rank != 0 && king_rank != 7)) return;

    // White kingside: king e1→g1, rook h1→f1
    if (side == Color::White && (cr & CASTLE_WHITE_KING)) {
        if ((occ & 0x60ULL) == 0                          // squares f1,g1 empty
            && !is_square_attacked(board, Square(4), Color::Black)
            && !is_square_attacked(board, Square(5), Color::Black)
            && !is_square_attacked(board, Square(6), Color::Black))
        {
            moves.add(Move::make(Square(4), Square(6), PieceType::King, -1, -1, MoveFlags::KingsideCastle));
        }
    }

    // White queenside: king e1→c1, rook a1→d1
    if (side == Color::White && (cr & CASTLE_WHITE_QUEEN)) {
        if ((occ & 0x0EULL) == 0                          // squares b1,c1,d1 empty
            && !is_square_attacked(board, Square(4), Color::Black)
            && !is_square_attacked(board, Square(3), Color::Black)
            && !is_square_attacked(board, Square(2), Color::Black))
        {
            moves.add(Move::make(Square(4), Square(2), PieceType::King, -1, -1, MoveFlags::QueensideCastle));
        }
    }

    // Black kingside: king e8→g8, rook h8→f8
    if (side == Color::Black && (cr & CASTLE_BLACK_KING)) {
        if ((occ & (0x60ULL << 56)) == 0                   // squares f8,g8 empty
            && !is_square_attacked(board, Square(60), Color::White)
            && !is_square_attacked(board, Square(61), Color::White)
            && !is_square_attacked(board, Square(62), Color::White))
        {
            moves.add(Move::make(Square(60), Square(62), PieceType::King, -1, -1, MoveFlags::KingsideCastle));
        }
    }

    // Black queenside: king e8→c8, rook a8→d8
    if (side == Color::Black && (cr & CASTLE_BLACK_QUEEN)) {
        if ((occ & (0x0EULL << 56)) == 0                   // squares b8,c8,d8 empty
            && !is_square_attacked(board, Square(60), Color::White)
            && !is_square_attacked(board, Square(59), Color::White)
            && !is_square_attacked(board, Square(58), Color::White))
        {
            moves.add(Move::make(Square(60), Square(58), PieceType::King, -1, -1, MoveFlags::QueensideCastle));
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void generate_pseudo_moves(const Board& board, MoveList& moves) {
    moves.clear();
    Color side = board.side_to_move();
    Color opp = opposite(side);
    uint64_t occ = board.all_pieces();
    uint64_t us = board.by_color(side);
    uint64_t them = board.by_color(opp);
    uint64_t empty = ~occ;
    const auto& att = get_attack_tables();

    // Pawns
    gen_pawn_moves(board, side, opp, us, them, empty, att, moves);

    // Knights
    gen_slider_moves(board, side, opp, us, them, PieceType::Knight,
                     [](uint64_t, Square sq) -> uint64_t {
                         return get_attack_tables().knight[sq.index];
                     },
                     moves);

    // Bishops
    gen_slider_moves(board, side, opp, us, them, PieceType::Bishop, bishop_attacks, moves);

    // Rooks
    gen_slider_moves(board, side, opp, us, them, PieceType::Rook, rook_attacks, moves);

    // Queens
    gen_slider_moves(board, side, opp, us, them, PieceType::Queen, queen_attacks, moves);

    // King
    gen_king_moves(board, side, opp, us, them, att, moves);

    // Castling
    gen_castling_moves(board, side, opp, occ, moves);
}

void generate_legal_moves(Board& board, MoveList& moves) {
    moves.clear();
    MoveList pseudo;
    pseudo.clear();
    generate_pseudo_moves(board, pseudo);

    for (int i = 0; i < pseudo.size(); i++) {
        Move m = pseudo[i];
        board.make_move(m);
        // After make_move, side_to_move is the opponent.
        // Check if our king (the side that just moved) is in check.
        if (!board.is_check_for(opposite(board.side_to_move()))) {
            moves.add(m);
        }
        board.unmake_move();
    }
}

int count_legal_moves(Board& board) {
    MoveList moves;
    generate_legal_moves(board, moves);
    return moves.size();
}

} // namespace driftwood
