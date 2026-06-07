#include "driftwood/tt.hpp"
#include "driftwood/types.hpp"
#include <cstdlib>
#include <cstring>
#include <new>

namespace driftwood {

// ---------------------------------------------------------------------------
// Move encoding
// ---------------------------------------------------------------------------

uint16_t tt_encode_move_from_move(Move m) {
    int promo = m.has_promotion() ? static_cast<int>(m.promotion()) + 1 : 0;
    return static_cast<uint16_t>(m.from().index
                                 | (m.to().index << 6)
                                 | (static_cast<uint16_t>(promo) << 12));
}

// ---------------------------------------------------------------------------
// Table management
// ---------------------------------------------------------------------------

TranspositionTable::~TranspositionTable() {
    std::free(table_);
}

void TranspositionTable::resize(size_t mb) {
    std::free(table_);
    table_ = nullptr;
    entries_ = 0;
    mb_ = 0;
    mask_ = 0;

    if (mb == 0) return;

    // Compute the number of entries (power of two)
    size_t desired = (mb * 1024ULL * 1024ULL) / sizeof(TTEntry);
    // Round down to power of two
    size_t n = 1;
    while (n * 2 <= desired) n *= 2;
    entries_ = n;
    mb_ = n * sizeof(TTEntry) / (1024 * 1024);
    mask_ = entries_ - 1;

    table_ = static_cast<TTEntry*>(std::aligned_alloc(64, entries_ * sizeof(TTEntry)));
    if (!table_) {
        // fallback to plain malloc if aligned_alloc fails
        table_ = static_cast<TTEntry*>(std::malloc(entries_ * sizeof(TTEntry)));
    }
    clear();
}

void TranspositionTable::clear() {
    if (table_) {
        std::memset(table_, 0, entries_ * sizeof(TTEntry));
    }
}

// ---------------------------------------------------------------------------
// Probe / Store
// ---------------------------------------------------------------------------

bool TranspositionTable::probe(uint64_t key, uint16_t& move, int& score,
                                int& depth, Bound& bound) const {
    if (!table_) return false;
    const TTEntry& entry = entry_for(key);
    uint16_t ku = static_cast<uint16_t>(key >> 48);
    if (entry.key_upper != ku) return false;

    move = entry.move;
    score = static_cast<int>(entry.score);
    depth = static_cast<int>(entry.depth());
    bound = entry.bound();
    return true;
}

void TranspositionTable::store(uint64_t key, uint16_t move, int score,
                                int depth, Bound bound, uint8_t age) {
    if (!table_) return;

    TTEntry& entry = entry_for(key);
    uint16_t ku = static_cast<uint16_t>(key >> 48);
    uint16_t d = static_cast<uint16_t>(depth);

    // Depth-preferred replacement
    if (entry.key_upper == ku) {
        // Same position: always replace
    } else if (entry.generation() != age) {
        // Older generation: replace
    } else if (d > entry.depth()) {
        // Same generation, deeper search: replace
    } else {
        // Keep the existing deeper entry
        return;
    }

    entry.key_upper = ku;
    entry.move = move;
    entry.score = static_cast<int16_t>(score);
    entry.set_depth(d);
    entry.set_bound(bound);
    entry.set_generation(age);
}

uint16_t TranspositionTable::probe_move(uint64_t key) const {
    if (!table_) return 0;
    const TTEntry& entry = entry_for(key);
    uint16_t ku = static_cast<uint16_t>(key >> 48);
    return (entry.key_upper == ku) ? entry.move : 0;
}

void TranspositionTable::prefetch(uint64_t key) const {
    if (!table_) return;
    // Hint: load the cache line for this entry
    __builtin_prefetch(&entry_for(key));
}

} // namespace driftwood
