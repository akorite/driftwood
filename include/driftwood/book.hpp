#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <random>

namespace driftwood {

class Board;

// ---------------------------------------------------------------------------
// BookEntry: 12 bytes
//   [0-7]   zobrist_key (uint64_t)
//   [8-9]   move (uint16_t, encoded same as TT: from:6, to:6, promotion:3, resv:1)
//   [10-11] weight (uint16_t)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct BookEntry {
    uint64_t key;
    uint16_t move;
    uint16_t weight;
};
#pragma pack(pop)

static_assert(sizeof(BookEntry) == 12, "BookEntry must be 12 bytes");

// ---------------------------------------------------------------------------
// OpeningBook
// ---------------------------------------------------------------------------

class OpeningBook {
public:
    OpeningBook() = default;
    ~OpeningBook();

    // Load a book from a binary file. Returns true on success.
    bool load(const std::string& filepath);

    // Returns true if the book is loaded and has entries.
    bool is_loaded() const { return !entries_.empty(); }

    // Probe the book for a position. Returns a move (encoded as uint16_t),
    // or 0 if not found. If multiple moves exist for the key, picks one
    // weighted by weight.
    uint16_t probe(uint64_t key) const;

    // Number of entries loaded
    size_t size() const { return entries_.size(); }

    // Number of unique positions
    size_t num_positions() const;

private:
    struct Entry {
        uint64_t key;
        uint16_t move;
        uint16_t weight;
    };

    std::vector<Entry> entries_;

    // Find range of entries matching the given key
    struct EntryRange {
        size_t start;
        size_t count;
    };
    EntryRange find_key(uint64_t key) const;
};

// ---------------------------------------------------------------------------
// Book generation helper
// ---------------------------------------------------------------------------

// Generate a small default book from hardcoded common openings
// and write it to `filepath`. Returns true on success.
bool generate_default_book(const std::string& filepath);

// Encode a move into the 16-bit book format
uint16_t book_encode_move(int from_sq, int to_sq, int promotion);

// Decode a 16-bit book move
void book_decode_move(uint16_t encoded, int& from, int& to, int& promotion);

} // namespace driftwood
