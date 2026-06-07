#pragma once

#include <cstdint>
#include <string>
#include <cassert>

namespace driftwood {

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------

enum class Color : int {
    White = 0,
    Black = 1,
};

constexpr int to_int(Color c) { return static_cast<int>(c); }

constexpr Color opposite(Color c) {
    return static_cast<Color>(to_int(c) ^ 1);
}

// ---------------------------------------------------------------------------
// PieceType
// ---------------------------------------------------------------------------

enum class PieceType : int {
    Pawn = 0,
    Knight = 1,
    Bishop = 2,
    Rook = 3,
    Queen = 4,
    King = 5,
};

constexpr int NUM_PIECE_TYPES = 6;

constexpr PieceType ALL_PIECE_TYPES[6] = {
    PieceType::Pawn, PieceType::Knight, PieceType::Bishop,
    PieceType::Rook, PieceType::Queen, PieceType::King
};

constexpr PieceType PROMOTION_TYPES[4] = {
    PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen
};

constexpr PieceType piece_type_from_int(int v) {
    return static_cast<PieceType>(v);
}

inline bool piece_type_from_char(char c, PieceType& out_pt, Color& out_color) {
    switch (c) {
        case 'P': out_pt = PieceType::Pawn;   out_color = Color::White; return true;
        case 'N': out_pt = PieceType::Knight; out_color = Color::White; return true;
        case 'B': out_pt = PieceType::Bishop; out_color = Color::White; return true;
        case 'R': out_pt = PieceType::Rook;   out_color = Color::White; return true;
        case 'Q': out_pt = PieceType::Queen;  out_color = Color::White; return true;
        case 'K': out_pt = PieceType::King;   out_color = Color::White; return true;
        case 'p': out_pt = PieceType::Pawn;   out_color = Color::Black; return true;
        case 'n': out_pt = PieceType::Knight; out_color = Color::Black; return true;
        case 'b': out_pt = PieceType::Bishop; out_color = Color::Black; return true;
        case 'r': out_pt = PieceType::Rook;   out_color = Color::Black; return true;
        case 'q': out_pt = PieceType::Queen;  out_color = Color::Black; return true;
        case 'k': out_pt = PieceType::King;   out_color = Color::Black; return true;
        default: return false;
    }
}

constexpr char piece_type_to_char(PieceType pt, Color c) {
    constexpr const char* white_chars = "PNBRQK";
    constexpr const char* black_chars = "pnbrqk";
    int idx = static_cast<int>(pt);
    if (idx < 0 || idx > 5) return '?';
    return (c == Color::White) ? white_chars[idx] : black_chars[idx];
}

constexpr char promotion_to_char(PieceType pt) {
    switch (pt) {
        case PieceType::Knight: return 'n';
        case PieceType::Bishop: return 'b';
        case PieceType::Rook:   return 'r';
        case PieceType::Queen:  return 'q';
        default:                return '?';
    }
}

// ---------------------------------------------------------------------------
// Square (0-63, a1=0, h1=7, a2=8, ..., h8=63)
// ---------------------------------------------------------------------------

struct Square {
    uint8_t index = 0;

    Square() = default;

    constexpr explicit Square(uint8_t idx) : index(idx) {
        assert(idx < 64);
    }

    constexpr static Square from_rank_file(uint8_t rank, uint8_t file) {
        assert(rank < 8 && file < 8);
        return Square(rank * 8 + file);
    }

    constexpr uint8_t rank() const { return index / 8; }
    constexpr uint8_t file() const { return index % 8; }
    constexpr uint64_t bitboard() const { return 1ULL << index; }

    static Square from_name(const std::string& name) {
        if (name.size() != 2) return Square(0); // caller must check
        uint8_t f = static_cast<uint8_t>(name[0] - 'a');
        uint8_t r = static_cast<uint8_t>(name[1] - '1');
        if (f < 8 && r < 8) return Square(r * 8 + f);
        return Square(0); // invalid
    }

    std::string name() const {
        char f = static_cast<char>('a' + file());
        char r = static_cast<char>('1' + rank());
        return {f, r};
    }
};

constexpr bool operator==(Square a, Square b) { return a.index == b.index; }
constexpr bool operator!=(Square a, Square b) { return a.index != b.index; }

// ---------------------------------------------------------------------------
// MoveFlags
// ---------------------------------------------------------------------------

enum class MoveFlags : uint8_t {
    Normal = 0,
    DoublePawnPush = 1,
    KingsideCastle = 2,
    QueensideCastle = 3,
    EnPassant = 4,
};

// ---------------------------------------------------------------------------
// Move (packed uint32_t)
//   bits  0–5    from square (0-63)
//   bits  6–11   to square (0-63)
//   bits 12–14   piece type (0-5)
//   bits 15–17   captured piece type (7 = none)
//   bits 18–20   promotion piece type (7 = none)
//   bits 21–23   move flags (0-4)
// ---------------------------------------------------------------------------

constexpr uint32_t NO_PIECE_MASK = 7;

struct Move {
    uint32_t data = 0;

    Move() = default;

    static Move make(Square from, Square to, PieceType piece,
                     int captured, int promotion, MoveFlags flags) {
        Move m;
        m.data = (static_cast<uint32_t>(from.index))
               | (static_cast<uint32_t>(to.index) << 6)
               | (static_cast<uint32_t>(piece) << 12)
               | (static_cast<uint32_t>(captured == -1 ? NO_PIECE_MASK : captured) << 15)
               | (static_cast<uint32_t>(promotion == -1 ? NO_PIECE_MASK : promotion) << 18)
               | (static_cast<uint32_t>(flags) << 21);
        return m;
    }

    Square from() const { return Square(static_cast<uint8_t>(data & 0x3F)); }
    Square to() const   { return Square(static_cast<uint8_t>((data >> 6) & 0x3F)); }

    PieceType piece() const {
        return static_cast<PieceType>((data >> 12) & 7);
    }

    int raw_captured() const { return static_cast<int>((data >> 15) & 7); }
    int raw_promotion() const { return static_cast<int>((data >> 18) & 7); }

    bool has_captured() const { return raw_captured() != static_cast<int>(NO_PIECE_MASK); }
    bool has_promotion() const { return raw_promotion() != static_cast<int>(NO_PIECE_MASK); }

    PieceType captured() const {
        return static_cast<PieceType>(raw_captured());
    }

    PieceType promotion() const {
        return static_cast<PieceType>(raw_promotion());
    }

    MoveFlags flags() const {
        return static_cast<MoveFlags>((data >> 21) & 7);
    }

    bool is_capture() const { return has_captured(); }
    bool is_promotion() const { return has_promotion(); }

    bool is_castle() const {
        auto f = flags();
        return f == MoveFlags::KingsideCastle || f == MoveFlags::QueensideCastle;
    }

    bool is_en_passant() const { return flags() == MoveFlags::EnPassant; }
    bool is_double_push() const { return flags() == MoveFlags::DoublePawnPush; }

    std::string to_uci() const {
        std::string s = from().name() + to().name();
        if (has_promotion()) s += promotion_to_char(promotion());
        return s;
    }
};

constexpr bool operator==(Move a, Move b) { return a.data == b.data; }
constexpr bool operator!=(Move a, Move b) { return a.data != b.data; }

// ---------------------------------------------------------------------------
// CastlingRights constants (bitmask)
// ---------------------------------------------------------------------------

constexpr uint8_t CASTLE_WHITE_KING  = 1;
constexpr uint8_t CASTLE_WHITE_QUEEN = 2;
constexpr uint8_t CASTLE_BLACK_KING  = 4;
constexpr uint8_t CASTLE_BLACK_QUEEN = 8;

constexpr uint8_t CASTLE_ALL = CASTLE_WHITE_KING | CASTLE_WHITE_QUEEN | CASTLE_BLACK_KING | CASTLE_BLACK_QUEEN;

constexpr uint8_t castling_side_mask(Color c) {
    return (c == Color::White) ? (CASTLE_WHITE_KING | CASTLE_WHITE_QUEEN)
                               : (CASTLE_BLACK_KING | CASTLE_BLACK_QUEEN);
}

inline std::string castling_to_fen(uint8_t rights) {
    std::string s;
    if (rights & CASTLE_WHITE_KING)  s += 'K';
    if (rights & CASTLE_WHITE_QUEEN) s += 'Q';
    if (rights & CASTLE_BLACK_KING)  s += 'k';
    if (rights & CASTLE_BLACK_QUEEN) s += 'q';
    if (s.empty()) s = '-';
    return s;
}

// ---------------------------------------------------------------------------
// MoveList (fixed-size, no heap allocation)
// ---------------------------------------------------------------------------

struct MoveList {
    static constexpr int MAX_MOVES = 256;
    Move moves[MAX_MOVES];
    int count = 0;

    void add(Move m) { moves[count++] = m; }
    int size() const { return count; }
    Move operator[](int i) const { return moves[i]; }
    void clear() { count = 0; }
};

// ---------------------------------------------------------------------------
// GameResult
// ---------------------------------------------------------------------------

enum class GameResult {
    Ongoing,
    Checkmate,   // winner is the Color that checkmated
    Stalemate,
    Draw50Move,
    DrawRepetition,
    DrawInsufficientMaterial,
};

} // namespace driftwood
