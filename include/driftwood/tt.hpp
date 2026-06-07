#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>

namespace driftwood {

// ---------------------------------------------------------------------------
// TT bounds
// ---------------------------------------------------------------------------

enum Bound : uint8_t {
    BOUND_EXACT = 0,
    BOUND_LOWER = 1, // fail-high (beta cutoff)
    BOUND_UPPER = 2, // fail-low (alpha not improved)
};

// ---------------------------------------------------------------------------
// TTEntry: 8 bytes per entry
//   [0-1]  key_upper  (upper 16 bits of the zobrist hash for verification)
//   [2-3]  move       (packed: from:6, to:6, promotion:3, reserved:1)
//   [4-5]  score      (int16_t)
//   [6-7]  depth_data (bits 0-9: depth, bits 10-11: bound, bits 12-15: age/gen)
// ---------------------------------------------------------------------------

struct TTEntry {
    uint16_t key_upper;
    uint16_t move;
    int16_t score;
    uint16_t depth_data;

    uint16_t depth() const { return depth_data & 0x3FF; }
    Bound bound() const { return static_cast<Bound>((depth_data >> 10) & 3); }
    uint8_t generation() const { return static_cast<uint8_t>((depth_data >> 12) & 0xF); }

    void set_depth(uint16_t d) {
        depth_data = (depth_data & ~0x3FFU) | (d & 0x3FFU);
    }
    void set_bound(Bound b) {
        depth_data = (depth_data & ~(3U << 10)) | (static_cast<uint16_t>(b) << 10);
    }
    void set_generation(uint8_t g) {
        depth_data = (depth_data & ~(0xFU << 12)) | (static_cast<uint16_t>(g) << 12);
    }
};

static_assert(sizeof(TTEntry) == 8, "TTEntry should be 8 bytes");

// ---------------------------------------------------------------------------
// TranspositionTable
// ---------------------------------------------------------------------------

class TranspositionTable {
public:
    TranspositionTable() = default;
    TranspositionTable(const TranspositionTable&) = delete;
    TranspositionTable& operator=(const TranspositionTable&) = delete;
    ~TranspositionTable();

    void resize(size_t mb);
    void clear();

    // Probe: returns true on hit and fills move, score, depth, bound.
    // Move is 0 if none was stored.
    bool probe(uint64_t key, uint16_t& move, int& score,
               int& depth, Bound& bound) const;

    // Store an entry. `age` is the current search generation (0-15).
    void store(uint64_t key, uint16_t move, int score,
               int depth, Bound bound, uint8_t age);

    // Get the move from TT for hash-move ordering (returns 0 if none).
    uint16_t probe_move(uint64_t key) const;

    // Prefetch the cache line for this key.
    void prefetch(uint64_t key) const;

    size_t size() const { return entries_; }
    size_t mb() const { return mb_; }

private:
    TTEntry* table_ = nullptr;
    size_t entries_ = 0;
    size_t mb_ = 0;
    uint64_t mask_ = 0;
    mutable std::mutex mutex_;

public:
    void lock() const { mutex_.lock(); }
    void unlock() const { mutex_.unlock(); }

private:

    TTEntry& entry_for(uint64_t key) {
        return table_[key & mask_];
    }
    const TTEntry& entry_for(uint64_t key) const {
        return table_[key & mask_];
    }
};

// ---------------------------------------------------------------------------
// Move encoding helpers for TT (from+to+promotion in 16 bits)
// ---------------------------------------------------------------------------

uint16_t tt_encode_move_from_move(class Move m);

// Encode from, to, promotion into a uint16_t for the TT.
// promotion: -1 for none, or PieceType enum value.
inline uint16_t tt_encode_move_parts(int from, int to, int promotion) {
    uint16_t promo_bits = (promotion >= 0 && promotion <= 4)
                              ? static_cast<uint16_t>(promotion + 1)  // 1-5, 0=none
                              : 0;
    return static_cast<uint16_t>(static_cast<uint16_t>(from)
                                 | (static_cast<uint16_t>(to) << 6)
                                 | (promo_bits << 12));
}

// Decode just from+to; promotion 0 means none.
inline void tt_decode_move(uint16_t encoded, uint16_t& from,
                           uint16_t& to, int& promotion) {
    from = encoded & 0x3F;
    to = (encoded >> 6) & 0x3F;
    uint16_t p = (encoded >> 12) & 7;
    promotion = (p == 0) ? -1 : static_cast<int>(p - 1);
}

} // namespace driftwood
