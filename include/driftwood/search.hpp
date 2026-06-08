#pragma once

#include "driftwood/types.hpp"
#include "driftwood/board.hpp"
#include "driftwood/tt.hpp"
#include "driftwood/book.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace driftwood {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr int MAX_PLY = 128;
constexpr int MATE_SCORE = 50000;
constexpr int MATE_IN_MAX = MATE_SCORE - MAX_PLY - 1;

// ---------------------------------------------------------------------------
// Search limits
// ---------------------------------------------------------------------------

struct SearchLimits {
    int depth = 64;          // maximum search depth
    int64_t nodes = -1;      // maximum nodes (-1 = unlimited)
    int64_t movetime = -1;   // fixed time per move (ms)
    int64_t wtime = -1;      // white remaining time
    int64_t btime = -1;      // black remaining time
    int64_t winc = 0;        // white increment
    int64_t binc = 0;        // black increment
    int movestogo = -1;      // moves until next time control
    bool infinite = false;   // search until told to stop
};

// ---------------------------------------------------------------------------
// Search result
// ---------------------------------------------------------------------------

struct SearchResult {
    Move best_move;
    Move ponder_move;
};

// ---------------------------------------------------------------------------
// Per-ply search stack data
// ---------------------------------------------------------------------------

struct SearchStackEntry {
    Move current_move;
    int16_t static_eval = 0;
    uint16_t move_count = 0;
    bool in_check = false;
    int pv_length = 0;
    Move pv[MAX_PLY];
};

// ---------------------------------------------------------------------------
// Per-thread context for Lazy SMP
// Each thread in the thread pool gets its own copy of these tables.
// ---------------------------------------------------------------------------

struct ThreadContext {
    Board board;
    Move killers_[MAX_PLY][3] = {{}};
    int history_[2][64][64] = {{}};
    uint16_t countermove_[64][64] = {{}};
    SearchStackEntry ss_[MAX_PLY] = {};
    uint64_t nodes_ = 0;
    int sel_depth_ = 0;
    int64_t start_time_ms_ = 0;
    int64_t max_time_ms_ = 5000;
    std::string pv_string_;
    int last_score_ = 0;
    int thread_id_ = 0;
};

// ---------------------------------------------------------------------------
// Searcher
// ---------------------------------------------------------------------------

class Searcher {
public:
    Searcher() = default;

    void set_tt(TranspositionTable* tt) { tt_ = tt; }
    void set_limits(const SearchLimits& lim) { limits_ = lim; }
    void set_stop_flag(std::atomic<bool>* flag) { stop_ = flag; }
    void set_book(OpeningBook* book) { book_ = book; }
    void set_num_threads(int n) { num_threads_ = n; }
    int num_threads() const { return num_threads_; }

    // Single-threaded search (backward compatible)
    SearchResult search(Board& board);

    // Multi-threaded Lazy SMP search
    SearchResult search_parallel(Board& board, int num_threads);

    const std::string& last_pv_string() const { return pv_string_; }
    int last_score() const { return last_score_; }
    uint64_t last_nodes() const { return last_nodes_; }

    // Book probing for root
    uint16_t probe_book(const Board& board) const;

private:
    // References (not owned)
    TranspositionTable* tt_ = nullptr;
    OpeningBook* book_ = nullptr;
    std::atomic<bool>* stop_ = nullptr;

    // Current search state
    SearchLimits limits_;
    int num_threads_ = 1;

    // Last results (preserved from last single-threaded search)
    std::string pv_string_;
    int last_score_ = 0;
    uint64_t last_nodes_ = 0;

    // Generation counter (shared across threads)
    uint8_t generation_ = 0;
    mutable std::unique_ptr<std::mutex> io_mutex_ = std::make_unique<std::mutex>();

    // -----------------------------------------------------------------------
    // Worker function for a single thread in the Lazy SMP pool.
    // Each thread runs iterative deepening independently, sharing the TT.
    // -----------------------------------------------------------------------
    void search_worker(ThreadContext& ctx,
                       const SearchLimits& limits,
                       uint8_t generation,
                       int thread_id,
                       int total_threads,
                       std::atomic<int>& max_depth_reported);

    // -----------------------------------------------------------------------
    // Internal search methods (operate on a ThreadContext)
    // -----------------------------------------------------------------------
    int pvs(ThreadContext& ctx, int depth, int ply, int alpha, int beta, bool cut_node);
    int qsearch(ThreadContext& ctx, int ply, int alpha, int beta);

    // Move ordering
    int pick_best(MoveList& moves, int* scores, int idx) const;

    // Helpers
    int evaluate(const ThreadContext& ctx) const;
    bool time_up(const ThreadContext& ctx) const;
    bool is_draw(const ThreadContext& ctx) const;
    int64_t current_time_ms() const;
    void update_pv(ThreadContext& ctx, int ply, Move move, const SearchStackEntry& child);
    void update_info(const ThreadContext& ctx, int depth, std::mutex* io_mutex);
};

} // namespace driftwood
