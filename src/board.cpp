#include "driftwood/board.hpp"
#include "driftwood/movegen.hpp"
#include <sstream>
#include <cctype>

namespace driftwood {

// ---------------------------------------------------------------------------
// Zobrist hashing
// ---------------------------------------------------------------------------

// Splitmix64 PRNG
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed) {}
    uint64_t next() {
        state += 0x9e3779b97f4a7c15ULL;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
};

struct ZobristTable {
    uint64_t pieces[2][6][64];
    uint64_t side;
    uint64_t castling[16];
    uint64_t ep[8];
};

static const ZobristTable& get_zobrist() {
    static const ZobristTable table = []{
        ZobristTable t{};
        Rng rng(123456789);
        for (int c = 0; c < 2; c++)
            for (int pt = 0; pt < 6; pt++)
                for (int sq = 0; sq < 64; sq++)
                    t.pieces[c][pt][sq] = rng.next();
        t.side = rng.next();
        for (int i = 0; i < 16; i++) t.castling[i] = rng.next();
        for (int i = 0; i < 8; i++) t.ep[i] = rng.next();
        return t;
    }();
    return table;
}

// ---------------------------------------------------------------------------
// Board construction
// ---------------------------------------------------------------------------

Board::Board() = default;

Board Board::starting_position() {
    return Board::from_fen(DEFAULT_FEN);
}

Board Board::from_fen(const std::string& fen) {
    // Split on whitespace
    std::istringstream iss(fen);
    std::string parts[6];
    int nparts = 0;
    std::string token;
    while (iss >> token && nparts < 6) {
        parts[nparts++] = token;
    }
    if (nparts < 4 || nparts > 6) {
        // Invalid FEN
        return Board();
    }

    Board board;

    // 1. Piece placement
    const std::string& placement = parts[0];
    // Split by '/'
    std::istringstream rank_stream(placement);
    std::string rank_str;
    int rank_idx = 0;
    while (std::getline(rank_stream, rank_str, '/')) {
        if (rank_idx >= 8) return Board(); // invalid
        int true_rank = 7 - rank_idx; // FEN rank 8 is board rank 7
        int file = 0;
        for (char ch : rank_str) {
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                file += (ch - '0');
            } else {
                if (file >= 8) return Board();
                PieceType pt;
                Color color;
                if (!piece_type_from_char(ch, pt, color)) return Board();
                Square sq = Square::from_rank_file(static_cast<uint8_t>(true_rank), static_cast<uint8_t>(file));
                board.set_piece_at(sq, color, pt);
                file++;
            }
        }
        if (file != 8) return Board();
        rank_idx++;
    }
    if (rank_idx != 8) return Board();

    // 2. Active color
    if (parts[1] == "w") {
        board.side_to_move_ = Color::White;
    } else if (parts[1] == "b") {
        board.side_to_move_ = Color::Black;
    } else {
        return Board();
    }

    // 3. Castling rights
    const std::string& castling_str = parts[2];
    if (castling_str != "-") {
        for (char ch : castling_str) {
            switch (ch) {
                case 'K': board.castling_rights_ |= CASTLE_WHITE_KING;  break;
                case 'Q': board.castling_rights_ |= CASTLE_WHITE_QUEEN; break;
                case 'k': board.castling_rights_ |= CASTLE_BLACK_KING;  break;
                case 'q': board.castling_rights_ |= CASTLE_BLACK_QUEEN; break;
                default: return Board();
            }
        }
    }

    // 4. En passant
    const std::string& ep_str = parts[3];
    if (ep_str != "-") {
        Square ep_sq = Square::from_name(ep_str);
        board.ep_square_ = static_cast<int>(ep_sq.index);
    }

    // 5. Halfmove clock (optional)
    if (nparts > 4) {
        board.halfmove_clock_ = std::stoi(parts[4]);
    }

    // 6. Fullmove number (optional)
    if (nparts > 5) {
        board.fullmove_number_ = std::stoi(parts[5]);
    }

    board.recompute_hash();
    return board;
}

std::string Board::to_fen() const {
    std::string placement;
    for (int rank_idx = 7; rank_idx >= 0; rank_idx--) {
        int empty = 0;
        for (int file = 0; file < 8; file++) {
            Square sq = Square::from_rank_file(static_cast<uint8_t>(rank_idx), static_cast<uint8_t>(file));
            Color c;
            PieceType pt;
            if (piece_at(sq, c, pt)) {
                if (empty > 0) {
                    placement += static_cast<char>('0' + empty);
                    empty = 0;
                }
                placement += piece_type_to_char(pt, c);
            } else {
                empty++;
            }
        }
        if (empty > 0) {
            placement += static_cast<char>('0' + empty);
        }
        if (rank_idx > 0) {
            placement += '/';
        }
    }

    const char* active = (side_to_move_ == Color::White) ? "w" : "b";
    std::string castling = castling_to_fen(castling_rights_);

    std::string ep;
    if (ep_square_ >= 0) {
        ep = Square(static_cast<uint8_t>(ep_square_)).name();
    } else {
        ep = "-";
    }

    return placement + " " + active + " " + castling + " " + ep + " "
         + std::to_string(halfmove_clock_) + " " + std::to_string(fullmove_number_);
}

// ---------------------------------------------------------------------------
// Bitboard accessors
// ---------------------------------------------------------------------------

uint64_t Board::piece_bb(Color c, PieceType pt) const {
    return pieces_[to_int(c)][static_cast<int>(pt)];
}

uint64_t Board::by_color(Color c) const {
    return by_color_[to_int(c)];
}

uint64_t Board::all_pieces() const { return all_pieces_; }
Color Board::side_to_move() const { return side_to_move_; }
uint8_t Board::castling_rights() const { return castling_rights_; }
int Board::ep_square() const { return ep_square_; }
int Board::halfmove_clock() const { return halfmove_clock_; }
int Board::fullmove_number() const { return fullmove_number_; }
uint64_t Board::hash() const { return hash_; }

const uint64_t* Board::pieces_for(Color c) const {
    return pieces_[to_int(c)];
}

bool Board::piece_at(Square sq, Color& out_color, PieceType& out_pt) const {
    uint64_t bb = sq.bitboard();
    for (int c = 0; c < 2; c++) {
        for (int pt = 0; pt < 6; pt++) {
            if (pieces_[c][pt] & bb) {
                out_color = static_cast<Color>(c);
                out_pt = static_cast<PieceType>(pt);
                return true;
            }
        }
    }
    return false;
}

int Board::find_king(Color c) const {
    uint64_t bb = pieces_[to_int(c)][static_cast<int>(PieceType::King)];
    if (bb == 0) return -1;
    return static_cast<int>(__builtin_ctzll(bb));
}

void Board::set_piece_at(Square sq, Color c, PieceType pt) {
    uint64_t bb = sq.bitboard();
    pieces_[to_int(c)][static_cast<int>(pt)] |= bb;
    by_color_[to_int(c)] |= bb;
    all_pieces_ |= bb;
}

// ---------------------------------------------------------------------------
// Hash computation
// ---------------------------------------------------------------------------

void Board::recompute_hash() {
    const auto& z = get_zobrist();
    uint64_t h = 0;
    for (int c = 0; c < 2; c++) {
        for (int pt = 0; pt < 6; pt++) {
            uint64_t bb = pieces_[c][pt];
            while (bb) {
                int sq = __builtin_ctzll(bb);
                h ^= z.pieces[c][pt][sq];
                bb &= bb - 1;
            }
        }
    }
    if (side_to_move_ == Color::Black) {
        h ^= z.side;
    }
    h ^= z.castling[castling_rights_ & 0xF];
    if (ep_square_ >= 0) {
        int file = ep_square_ % 8;
        h ^= z.ep[file];
    }
    hash_ = h;
}

// ---------------------------------------------------------------------------
// Null move
// ---------------------------------------------------------------------------

void Board::make_null_move() {
    StateInfo state;
    state.move = Move(); // null move: all zeros
    state.hash = hash_;
    state.castling_rights = castling_rights_;
    state.ep_square = ep_square_;
    state.halfmove_clock = halfmove_clock_;
    state.fullmove_number = fullmove_number_;
    history_[history_size_++] = state;

    const auto& z = get_zobrist();

    // Toggle side to move (remove old, add new)
    hash_ ^= z.side;

    // Clear en passant
    if (ep_square_ >= 0) {
        hash_ ^= z.ep[ep_square_ % 8];
        ep_square_ = -1;
    }

    side_to_move_ = opposite(side_to_move_);
}

void Board::unmake_null_move() {
    if (history_size_ == 0) return;
    const StateInfo& state = history_[--history_size_];

    side_to_move_ = opposite(side_to_move_);
    hash_ = state.hash;
    castling_rights_ = state.castling_rights;
    ep_square_ = state.ep_square;
    halfmove_clock_ = state.halfmove_clock;
    fullmove_number_ = state.fullmove_number;
}

// ---------------------------------------------------------------------------
// Threefold repetition
// ---------------------------------------------------------------------------

bool Board::has_threefold_repetition() const {
    int count = 1; // current position
    for (int i = history_size_ - 1; i >= 0; --i) {
        if (history_[i].hash == hash_) {
            ++count;
            if (count >= 3) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Make / Unmake move
// ---------------------------------------------------------------------------

void Board::make_move(Move m) {
    const auto& z = get_zobrist();
    Color side = side_to_move_;
    Color opp = opposite(side);

    Square from = m.from();
    Square to = m.to();
    PieceType piece = m.piece();
    bool has_cap = m.has_captured();
    PieceType captured_pt = m.captured();
    bool has_prom = m.has_promotion();
    PieceType promotion_pt = m.promotion();
    MoveFlags flags = m.flags();

    // Save state
    StateInfo state;
    state.move = m;
    state.hash = hash_;
    state.castling_rights = castling_rights_;
    state.ep_square = ep_square_;
    state.halfmove_clock = halfmove_clock_;
    state.fullmove_number = fullmove_number_;
    history_[history_size_++] = state;

    // ---- Update hash: remove current features ----
    hash_ ^= z.side; // toggle side to move
    if (ep_square_ >= 0) {
        hash_ ^= z.ep[ep_square_ % 8];
    }
    hash_ ^= z.castling[castling_rights_ & 0xF];

    // Remove piece from source square
    uint64_t from_bb = 1ULL << from.index;
    pieces_[to_int(side)][static_cast<int>(piece)] ^= from_bb;
    by_color_[to_int(side)] ^= from_bb;
    hash_ ^= z.pieces[to_int(side)][static_cast<int>(piece)][from.index];

    // Handle capture
    Square captured_sq = (flags == MoveFlags::EnPassant)
        ? Square(static_cast<uint8_t>((side == Color::White) ? to.index - 8 : to.index + 8))
        : to;
    if (has_cap) {
        uint64_t cap_bb = 1ULL << captured_sq.index;
        pieces_[to_int(opp)][static_cast<int>(captured_pt)] ^= cap_bb;
        by_color_[to_int(opp)] ^= cap_bb;
        hash_ ^= z.pieces[to_int(opp)][static_cast<int>(captured_pt)][captured_sq.index];
    }

    // Handle castling: move the rook
    if (flags == MoveFlags::KingsideCastle || flags == MoveFlags::QueensideCastle) {
        Square r_from, r_to;
        castle_rook_squares(side, flags, r_from, r_to);
        uint64_t r_from_bb = 1ULL << r_from.index;
        uint64_t r_to_bb = 1ULL << r_to.index;
        pieces_[to_int(side)][static_cast<int>(PieceType::Rook)] ^= r_from_bb | r_to_bb;
        by_color_[to_int(side)] ^= r_from_bb | r_to_bb;
        hash_ ^= z.pieces[to_int(side)][static_cast<int>(PieceType::Rook)][r_from.index];
        hash_ ^= z.pieces[to_int(side)][static_cast<int>(PieceType::Rook)][r_to.index];
    }

    // Add piece to destination (possibly promoted)
    uint64_t to_bb = 1ULL << to.index;
    PieceType dest_pt = has_prom ? promotion_pt : piece;
    pieces_[to_int(side)][static_cast<int>(dest_pt)] ^= to_bb;
    by_color_[to_int(side)] ^= to_bb;
    hash_ ^= z.pieces[to_int(side)][static_cast<int>(dest_pt)][to.index];

    // Recompute all_pieces
    all_pieces_ = by_color_[0] | by_color_[1];

    // ---- Update state ----
    side_to_move_ = opp;

    // Castling rights
    uint8_t old_rights = castling_rights_;
    uint8_t new_rights = old_rights;
    // King moved
    if (piece == PieceType::King) {
        new_rights &= ~castling_side_mask(side);
    }
    // Rook moved from starting square
    if (piece == PieceType::Rook) {
        if (from.index == 0)      new_rights &= ~CASTLE_WHITE_QUEEN;
        else if (from.index == 7) new_rights &= ~CASTLE_WHITE_KING;
        else if (from.index == 56) new_rights &= ~CASTLE_BLACK_QUEEN;
        else if (from.index == 63) new_rights &= ~CASTLE_BLACK_KING;
    }
    // Rook captured
    if (has_cap && captured_pt == PieceType::Rook) {
        if (captured_sq.index == 0)      new_rights &= ~CASTLE_WHITE_QUEEN;
        else if (captured_sq.index == 7) new_rights &= ~CASTLE_WHITE_KING;
        else if (captured_sq.index == 56) new_rights &= ~CASTLE_BLACK_QUEEN;
        else if (captured_sq.index == 63) new_rights &= ~CASTLE_BLACK_KING;
    }
    castling_rights_ = new_rights;

    // En passant square
    if (flags == MoveFlags::DoublePawnPush) {
        uint8_t ep_rank = (side == Color::White) ? to.rank() - 1 : to.rank() + 1;
        ep_square_ = Square::from_rank_file(ep_rank, to.file()).index;
    } else {
        ep_square_ = -1;
    }

    // Halfmove clock
    if (piece == PieceType::Pawn || has_cap) {
        halfmove_clock_ = 0;
    } else {
        halfmove_clock_++;
    }

    // Fullmove number
    if (side == Color::Black) {
        fullmove_number_++;
    }

    // ---- Update hash: add new features ----
    if (ep_square_ >= 0) {
        hash_ ^= z.ep[ep_square_ % 8];
    }
    hash_ ^= z.castling[castling_rights_ & 0xF];
}

void Board::unmake_move() {
    if (history_size_ == 0) return;
    const StateInfo& state = history_[--history_size_];
    Move m = state.move;

    Color side = opposite(side_to_move_); // the side that made the move
    Color opp = opposite(side);

    Square from = m.from();
    Square to = m.to();
    PieceType piece = m.piece();
    bool has_cap = m.has_captured();
    PieceType captured_pt = m.captured();
    bool has_prom = m.has_promotion();
    PieceType dest_pt = has_prom ? m.promotion() : piece;
    MoveFlags flags = m.flags();

    uint64_t from_bb = 1ULL << from.index;
    uint64_t to_bb = 1ULL << to.index;

    // Remove piece from destination
    pieces_[to_int(side)][static_cast<int>(dest_pt)] ^= to_bb;
    by_color_[to_int(side)] ^= to_bb;

    // Add piece back at source
    pieces_[to_int(side)][static_cast<int>(piece)] ^= from_bb;
    by_color_[to_int(side)] ^= from_bb;

    // Restore captured piece
    if (has_cap) {
        Square cap_sq = (flags == MoveFlags::EnPassant)
            ? Square(static_cast<uint8_t>((side == Color::White) ? to.index - 8 : to.index + 8))
            : to;
        uint64_t cap_bb = 1ULL << cap_sq.index;
        pieces_[to_int(opp)][static_cast<int>(captured_pt)] ^= cap_bb;
        by_color_[to_int(opp)] ^= cap_bb;
    }

    // Restore rook for castling
    if (flags == MoveFlags::KingsideCastle || flags == MoveFlags::QueensideCastle) {
        Square r_from, r_to;
        castle_rook_squares(side, flags, r_from, r_to);
        pieces_[to_int(side)][static_cast<int>(PieceType::Rook)] ^=
            (1ULL << r_from.index) | (1ULL << r_to.index);
        by_color_[to_int(side)] ^= (1ULL << r_from.index) | (1ULL << r_to.index);
    }

    all_pieces_ = by_color_[0] | by_color_[1];

    // Restore state
    side_to_move_ = side;
    hash_ = state.hash;
    castling_rights_ = state.castling_rights;
    ep_square_ = state.ep_square;
    halfmove_clock_ = state.halfmove_clock;
    fullmove_number_ = state.fullmove_number;
}

// ---------------------------------------------------------------------------
// Check / mate / stalemate detection
// ---------------------------------------------------------------------------

bool Board::is_check() const {
    int king_sq = find_king(side_to_move_);
    if (king_sq < 0) return false;
    return is_square_attacked(*this, Square(static_cast<uint8_t>(king_sq)), opposite(side_to_move_));
}

bool Board::is_check_for(Color c) const {
    int king_sq = find_king(c);
    if (king_sq < 0) return false;
    return is_square_attacked(*this, Square(static_cast<uint8_t>(king_sq)), opposite(c));
}

bool Board::is_checkmate() const {
    return is_check() && !has_legal_move();
}

bool Board::is_stalemate() const {
    return !is_check() && !has_legal_move();
}

bool Board::is_insufficient_material() const {
    // King vs King
    if (__builtin_popcountll(all_pieces_) == 2) return true;
    // King + minor vs King
    if (__builtin_popcountll(all_pieces_) == 3) {
        int w_knights = __builtin_popcountll(pieces_[0][static_cast<int>(PieceType::Knight)]);
        int b_knights = __builtin_popcountll(pieces_[1][static_cast<int>(PieceType::Knight)]);
        int w_bishops = __builtin_popcountll(pieces_[0][static_cast<int>(PieceType::Bishop)]);
        int b_bishops = __builtin_popcountll(pieces_[1][static_cast<int>(PieceType::Bishop)]);
        if ((w_knights == 1 && b_knights == 0 && w_bishops == 0 && b_bishops == 0) ||
            (b_knights == 1 && w_knights == 0 && w_bishops == 0 && b_bishops == 0) ||
            (w_bishops == 1 && b_bishops == 0 && w_knights == 0 && b_knights == 0) ||
            (b_bishops == 1 && w_bishops == 0 && w_knights == 0 && b_knights == 0)) {
            return true;
        }
    }
    return false;
}

bool Board::has_legal_move() const {
    // Clone the board (trivially copyable except history; we discard history)
    Board clone = *this;
    clone.history_size_ = 0;
    MoveList moves;
    generate_legal_moves(clone, moves);
    return moves.size() > 0;
}

// ---------------------------------------------------------------------------
// Helper: castle rook squares
// ---------------------------------------------------------------------------

void castle_rook_squares(Color c, MoveFlags flags, Square& r_from, Square& r_to) {
    if (c == Color::White) {
        if (flags == MoveFlags::KingsideCastle) {
            r_from = Square(7);  // h1
            r_to   = Square(5);  // f1
        } else {
            r_from = Square(0);  // a1
            r_to   = Square(3);  // d1
        }
    } else {
        if (flags == MoveFlags::KingsideCastle) {
            r_from = Square(63); // h8
            r_to   = Square(61); // f8
        } else {
            r_from = Square(56); // a8
            r_to   = Square(59); // d8
        }
    }
}

// ---------------------------------------------------------------------------
// Make Move from UCI string
// ---------------------------------------------------------------------------

Move move_from_uci(const Board& board, const std::string& uci) {
    if (uci.size() < 4 || uci.size() > 5) return Move();

    Square from = Square::from_name(uci.substr(0, 2));
    Square to = Square::from_name(uci.substr(2, 2));

    Color piece_color;
    PieceType piece;
    if (!board.piece_at(from, piece_color, piece)) return Move();

    int promotion = -1;
    if (uci.size() == 5) {
        switch (uci[4]) {
            case 'n': promotion = static_cast<int>(PieceType::Knight); break;
            case 'b': promotion = static_cast<int>(PieceType::Bishop); break;
            case 'r': promotion = static_cast<int>(PieceType::Rook);   break;
            case 'q': promotion = static_cast<int>(PieceType::Queen);  break;
            default: return Move();
        }
    }

    Color cap_color;
    PieceType cap_pt;
    int captured = -1;
    if (board.piece_at(to, cap_color, cap_pt)) {
        captured = static_cast<int>(cap_pt);
    }

    // Determine flags
    MoveFlags flags = MoveFlags::Normal;
    if (piece == PieceType::King && from.file() == 4) {
        int file_diff = static_cast<int>(to.file()) - static_cast<int>(from.file());
        if (file_diff == 2) {
            flags = MoveFlags::KingsideCastle;
        } else if (file_diff == -2) {
            flags = MoveFlags::QueensideCastle;
        }
    } else if (piece == PieceType::Pawn && to.file() != from.file() &&
               board.ep_square() >= 0 && to.index == static_cast<uint8_t>(board.ep_square())) {
        flags = MoveFlags::EnPassant;
    } else if (piece == PieceType::Pawn) {
        int rank_diff = static_cast<int>(to.rank()) - static_cast<int>(from.rank());
        if (rank_diff == 2 || rank_diff == -2) {
            flags = MoveFlags::DoublePawnPush;
        }
    }

    return Move::make(from, to, piece, captured, promotion, flags);
}

} // namespace driftwood
