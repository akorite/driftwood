#include "driftwood/book.hpp"
#include "driftwood/board.hpp"
#include "driftwood/types.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

namespace driftwood {

// ---------------------------------------------------------------------------
// Open / close
// ---------------------------------------------------------------------------

OpeningBook::~OpeningBook() = default;

bool OpeningBook::load(const std::string& filepath) {
    entries_.clear();

    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        // File not found — not an error, just fall back to search
        return false;
    }

    // Get file size
    file.seekg(0, std::ios::end);
    size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (file_size % sizeof(BookEntry) != 0) {
        std::cerr << "info string Book file size " << file_size
                  << " not a multiple of " << sizeof(BookEntry) << std::endl;
        return false;
    }

    if (file_size == 0) {
        return false;
    }

    // Read all entries
    size_t num_entries = file_size / sizeof(BookEntry);
    std::vector<BookEntry> raw_entries(num_entries);
    file.read(reinterpret_cast<char*>(raw_entries.data()), static_cast<std::streamsize>(file_size));

    if (!file) {
        std::cerr << "info string Failed to read book file" << std::endl;
        return false;
    }

    // Copy to internal format
    entries_.reserve(num_entries);
    for (const auto& raw : raw_entries) {
        entries_.push_back({raw.key, raw.move, raw.weight});
    }

    // Sort by key for binary search
    std::sort(entries_.begin(), entries_.end(),
              [](const Entry& a, const Entry& b) { return a.key < b.key; });

    std::cerr << "info string Loaded book with " << entries_.size()
              << " entries (" << num_positions() << " positions)" << std::endl;
    return true;
}

size_t OpeningBook::num_positions() const {
    if (entries_.empty()) return 0;
    size_t count = 1;
    for (size_t i = 1; i < entries_.size(); ++i) {
        if (entries_[i].key != entries_[i - 1].key) ++count;
    }
    return count;
}

OpeningBook::EntryRange OpeningBook::find_key(uint64_t key) const {
    // Binary search for the first entry matching key
    auto cmp_key = [](const Entry& e, uint64_t k) { return e.key < k; };
    auto it = std::lower_bound(entries_.begin(), entries_.end(), key, cmp_key);

    if (it == entries_.end() || it->key != key) {
        return {0, 0};
    }

    size_t start = static_cast<size_t>(it - entries_.begin());
    size_t count = 0;
    while (start + count < entries_.size() && entries_[start + count].key == key) {
        ++count;
    }
    return {start, count};
}

uint16_t OpeningBook::probe(uint64_t key) const {
    if (entries_.empty()) return 0;

    auto range = find_key(key);
    if (range.count == 0) return 0;

    if (range.count == 1) {
        return entries_[range.start].move;
    }

    // Weighted random selection
    uint64_t total_weight = 0;
    for (size_t i = 0; i < range.count; ++i) {
        total_weight += entries_[range.start + i].weight;
    }

    if (total_weight == 0) return entries_[range.start].move;

    // Simple deterministic random from total weight
    // Use a static RNG with a small seed for reproducibility
    static std::mt19937_64 rng(1234567);
    uint64_t pick = rng() % total_weight;

    uint64_t accum = 0;
    for (size_t i = 0; i < range.count; ++i) {
        accum += entries_[range.start + i].weight;
        if (pick < accum) {
            return entries_[range.start + i].move;
        }
    }

    return entries_[range.start].move; // fallback
}

// ---------------------------------------------------------------------------
// Move encoding helpers
// ---------------------------------------------------------------------------

uint16_t book_encode_move(int from_sq, int to_sq, int promotion) {
    uint16_t promo_bits = (promotion >= 0 && promotion <= 4)
                              ? static_cast<uint16_t>(promotion + 1)
                              : 0;
    return static_cast<uint16_t>(
        static_cast<uint16_t>(from_sq)
        | (static_cast<uint16_t>(to_sq) << 6)
        | (promo_bits << 12));
}

void book_decode_move(uint16_t encoded, int& from, int& to, int& promotion) {
    from = encoded & 0x3F;
    to = (encoded >> 6) & 0x3F;
    uint16_t p = (encoded >> 12) & 7;
    promotion = (p == 0) ? -1 : static_cast<int>(p - 1);
}

// ---------------------------------------------------------------------------
// Default book generation from common openings
// ---------------------------------------------------------------------------

// Common opening lines: each is a sequence of UCI moves
// We record positions up to move 12 (24 half-moves) and book moves from each
struct OpeningLine {
    const char* moves; // space-separated UCI moves
    int weight;        // relative weight for this line
};

static const OpeningLine OPENING_LINES[] = {
    // Ruy Lopez (Spanish)
    {"e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 d2d3 d7d6", 10},
    {"e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5c6 d7c6 d2d4 e5d4", 8},
    {"e2e4 e7e5 g1f3 b8c6 f1b5 g8f6 d2d3 d7d6 c2c3 f8e7", 8},
    {"e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 0-0 f8e7 f1e1", 7},

    // Italian Game
    {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5", 10},
    {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d4 e5d4 e1g1", 8},
    {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4", 7},

    // Sicilian Defense
    {"e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6", 10},
    {"e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 d7d6", 8},
    {"e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6 b1c3 d8c7", 7},
    {"e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 g7g6", 7},

    // French Defense
    {"e2e4 e7e6 d2d4 d7d5 e4d5 e6d5 g1f3 g8f6", 10},
    {"e2e4 e7e6 d2d4 d7d5 b1c3 d5e4 c3e4 b8c6 g1f3 g8f6", 8},
    {"e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 b8c6 g1f3 d8b6", 7},

    // Queen's Gambit
    {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c4d5 e6d5", 10},
    {"d2d4 d7d5 c2c4 c7c6 c4d5 c6d5 b1c3 g8f6 g1f3 b8c6", 8},
    {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 g1f3 f8e7 c1g5 0-0 e2e3", 7},

    // King's Indian Defense
    {"d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 0-0", 10},
    {"d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f1e2 e7e5 d4d5", 8},

    // Nimzo-Indian Defense
    {"d2d4 g8f6 c2c4 e7e6 b1c3 f8b4", 10},
    {"d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 b7b6 g1f3 c8b7 f1d3", 7},

    // Caro-Kann Defense
    {"e2e4 c7c6 d2d4 d7d5 e4d5 c6d5 c1d3 b8c6 c2c3 g8f6", 10},
    {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6", 8},

    // English Opening
    {"c2c4 e7e5 b1c3 g8f6 g1f3 b8c6", 8},
    {"c2c4 c7c5 g1f3 g8f6 d2d4 c5d4 f3d4 e7e6", 7},

    // Pirc Defense
    {"e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 g1f3 f8g7 f1e2 0-0", 7},

    // Modern Defense
    {"e2e4 g7g6 d2d4 f8g7 b1c3 d7d6 g1f3 b8c6 f1e2 e7e5", 6},

    // Alekhine's Defense
    {"e2e4 g8f6 e4e5 f6d5 d2d4 d7d6 g1f3 d5c6", 6},

    // Scotch Game
    {"e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6", 8},
    {"e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 f8c5 d4b3 c5b6", 6},

    // Four Knights Game
    {"e2e4 e7e5 g1f3 b8c6 b1c3 g8f6 f1b5 f8b4", 7},

    // Vienna Game
    {"e2e4 e7e5 b1c3 g8f6 f1c4 b8c6 d2d3 f8c5", 6},

    // Petroff Defense
    {"e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5", 8},

    // Hungarian Defense
    {"e2e4 e7e5 g1f3 b8c6 f1c4 f8e7 d2d4 e5d4 f3d4 d7d6", 5},

    // Philidor Defense
    {"e2e4 e7e5 g1f3 d7d6 d2d4 e5d4 f3d4 g8f6 b1c3 f8e7", 6},
};

void write_book(const std::string& filepath) {
    // Collect all entries
    std::vector<BookEntry> entries;
    Board board;

    for (const auto& line : OPENING_LINES) {
        board = Board::starting_position();

        // Parse the move sequence
        std::string moves_str(line.moves);
        std::vector<std::string> uci_moves;
        size_t pos = 0;
        while ((pos = moves_str.find(' ')) != std::string::npos) {
            uci_moves.push_back(moves_str.substr(0, pos));
            moves_str.erase(0, pos + 1);
        }
        if (!moves_str.empty()) {
            uci_moves.push_back(moves_str);
        }

        // For each position in the line (except the last), record the next move
        // Also record the first move from the starting position
        for (size_t i = 0; i < uci_moves.size(); ++i) {
            // Record the move from the current position
            Move m = move_from_uci(board, uci_moves[i]);
            if (m.data == 0) break;

            int promo = m.has_promotion() ? static_cast<int>(m.promotion()) : -1;
            uint16_t enc = book_encode_move(m.from().index, m.to().index, promo);

            uint64_t key = board.hash();

            BookEntry entry;
            entry.key = key;
            entry.move = enc;
            entry.weight = static_cast<uint16_t>(line.weight);
            entries.push_back(entry);

            board.make_move(m);

            // Also record the move *from* this new position if there are more moves
            // (we want moves from each position along the line)
            // Actually the above records the move from each position.
            // For positions that appear in multiple lines, we record additional weights.
        }
    }

    if (entries.empty()) {
        std::cerr << "info string No book entries generated" << std::endl;
        return;
    }

    // Sort by key
    std::sort(entries.begin(), entries.end(),
              [](const BookEntry& a, const BookEntry& b) { return a.key < b.key; });

    // Write to file
    std::ofstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "info string Failed to write book file: " << filepath << std::endl;
        return;
    }

    file.write(reinterpret_cast<const char*>(entries.data()),
               static_cast<std::streamsize>(entries.size() * sizeof(BookEntry)));

    file.close();

    // Count unique keys
    size_t unique = 0;
    if (!entries.empty()) {
        unique = 1;
        for (size_t i = 1; i < entries.size(); ++i) {
            if (entries[i].key != entries[i-1].key) ++unique;
        }
    }

    std::cerr << "info string Wrote book: " << entries.size()
              << " entries, " << unique << " unique positions to "
              << filepath << std::endl;
}

bool generate_default_book(const std::string& filepath) {
    write_book(filepath);
    std::ifstream test(filepath);
    return test.good();
}

} // namespace driftwood
