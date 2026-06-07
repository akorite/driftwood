#pragma once

#include "driftwood/types.hpp"
#include <string>

namespace driftwood {

// ---------------------------------------------------------------------------
// StateInfo for undo
// ---------------------------------------------------------------------------

struct StateInfo {
    Move move;
    uint64_t hash;
    uint8_t castling_rights;
    int ep_square; // -1 for none
    int halfmove_clock;
    int fullmove_number;
};

// ---------------------------------------------------------------------------
// Board
// ---------------------------------------------------------------------------

class Board {
public:
    Board();

    static Board starting_position();
    static Board from_fen(const std::string& fen);
    std::string to_fen() const;

    void make_move(Move m);
    void unmake_move();

    // Accessors
    uint64_t piece_bb(Color c, PieceType pt) const;
    uint64_t by_color(Color c) const;
    uint64_t all_pieces() const;
    Color side_to_move() const;
    uint8_t castling_rights() const;
    int ep_square() const; // -1 for none
    int halfmove_clock() const;
    int fullmove_number() const;
    uint64_t hash() const;

    // Expose for movegen
    const uint64_t* pieces_for(Color c) const;

    // Check/mate detection
    bool is_check() const;
    bool is_check_for(Color c) const;
    bool is_checkmate() const;
    bool is_stalemate() const;
    bool is_insufficient_material() const;

    // Null move support
    void make_null_move();
    void unmake_null_move();

    // Repetition detection (threefold)
    bool has_threefold_repetition() const;

    // Piece lookup
    bool piece_at(Square sq, Color& out_color, PieceType& out_pt) const;

    // King location
    int find_king(Color c) const; // returns square index or -1

private:
    uint64_t pieces_[2][6] = {{0}};
    uint64_t by_color_[2] = {0};
    uint64_t all_pieces_ = 0;
    Color side_to_move_ = Color::White;
    uint8_t castling_rights_ = 0;
    int ep_square_ = -1;         // -1 = none
    int halfmove_clock_ = 0;
    int fullmove_number_ = 1;
    uint64_t hash_ = 0;

    // History stack (fixed-size)
    static constexpr int MAX_PLY = 2048;
    StateInfo history_[MAX_PLY];
    int history_size_ = 0;

    void set_piece_at(Square sq, Color c, PieceType pt);
    void recompute_hash();
    bool has_legal_move() const;
};

// Default starting position FEN
inline constexpr const char* DEFAULT_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Helper: castle rook source/destination squares
void castle_rook_squares(Color c, MoveFlags flags, Square& r_from, Square& r_to);

// Make Move from UCI string
Move move_from_uci(const Board& board, const std::string& uci);

} // namespace driftwood
