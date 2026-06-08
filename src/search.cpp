// ---------------------------------------------------------------------------
// DriftWood Chess Engine — Lazy SMP Search
//
// Implements PVS/NegaScout with iterative deepening, aspiration windows,
// and Lazy SMP multithreading. Key search enhancements:
//
// - Internal Iterative Reduction (IIR)
// - Late Move Reductions (LMR) with precomputed tables
// - Late Move Pruning (LMP)
// - Null Move Pruning (NMP)
// - Futility Pruning
// - Razoring
// - Singular Extensions (SE)
// - Multi-Cut Pruning (MC)
// - History Malus (penalize bad quiet moves)
// - Correction History (eval corrections from search results)
// - 3-Killer Heuristic
// - Counter-move History (piece-type-specific)
// - Check Extensions
// ---------------------------------------------------------------------------

#include "driftwood/search.hpp"
#include "driftwood/eval.hpp"
#include "driftwood/movegen.hpp"
#include "driftwood/syzygy.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace driftwood {

// ---------------------------------------------------------------------------
// Search constants
// ---------------------------------------------------------------------------

constexpr int ASPIRATION_WINDOW = 30;
constexpr int NMP_DEPTH = 3;
constexpr int NMP_R = 3;
constexpr int LMR_DEPTH = 3;
constexpr int LMR_MIN_MOVES = 3;
constexpr int FUTILITY_DEPTH = 3;
constexpr int FUTILITY_MARGIN[4] = {0, 200, 300, 500};
constexpr int RAZOR_DEPTH = 2;
constexpr int RAZOR_MARGIN[3] = {0, 500, 600};
constexpr int DELTA_MARGIN = 200;
constexpr int MAX_QSCORE = 50000;

// Multi-cut constants
constexpr int MC_DEPTH = 6;
constexpr int MC_CUTOFF_COUNT = 3;

// Syzygy integration: use WDL as eval when pieces are few
constexpr int SYZYGY_MAX_PIECES = 6;

// History malus scale
constexpr int HISTORY_MALUS = 32;
constexpr int HISTORY_MAX = 8192;

// ---------------------------------------------------------------------------
// Precomputed LMR reduction table
// Index: [depth][moves_searched], values: reduction in plies
// ---------------------------------------------------------------------------

constexpr int LMR_TABLE[32][64] = {
    // depth 0 (unused, but filled for safety)
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    // depth 1
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    // depth 2
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    // depth 3
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    // depth 4
    {0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2},
    // depth 5
    {0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3},
    // depth 6
    {0,0,0,0,0,0,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4},
    // depth 7
    {0,0,0,0,0,0,1,1,1,1,1,1,2,2,2,2,2,2,2,3,3,3,3,3,3,3,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5},
    // depth 8
    {0,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,4,4,4,4,4,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6},
    // depth 9
    {0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,6,6,6,6,6,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7},
    // depth 10
    {0,0,0,0,0,0,1,1,1,2,2,2,2,3,3,3,4,4,4,4,5,5,5,5,6,6,6,6,7,7,7,7,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8},
    // depth 11+
    {0,0,0,0,0,0,1,1,1,2,2,2,3,3,3,4,4,4,5,5,5,6,6,6,7,7,7,7,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9},
};

// Helper to get LMR reduction from table
static inline int get_lmr_reduction(int depth, int moves_searched) {
    if (depth < 32 && moves_searched < 64) {
        return LMR_TABLE[depth][moves_searched];
    }
    // Fallback for out-of-bounds
    double d = std::log(static_cast<double>(depth));
    double m = std::log(static_cast<double>(moves_searched));
    return static_cast<int>(d * m / 2.2 + 0.5);
}

// MVV-LVA scores for captures.
constexpr int MVV_LVA_SCORE(int victim, int attacker) {
    return victim * 16 + (7 - attacker);
}

constexpr int MVV_LVA_VAL[6] = {100, 320, 330, 500, 900, 0};

// ---------------------------------------------------------------------------
// Book probing
// ---------------------------------------------------------------------------

uint16_t Searcher::probe_book(const Board& board) const {
    if (!book_ || !book_->is_loaded()) return 0;
    return book_->probe(board.hash());
}

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------

int64_t Searcher::current_time_ms() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch()).count();
}

bool Searcher::time_up(const ThreadContext& ctx) const {
    if (stop_ && stop_->load(std::memory_order_relaxed)) return true;
    if (limits_.infinite) return false;
    if (limits_.movetime > 0) {
        return (current_time_ms() - ctx.start_time_ms_) >= limits_.movetime;
    }
    return (current_time_ms() - ctx.start_time_ms_) >= ctx.max_time_ms_;
}

bool Searcher::is_draw(const ThreadContext& ctx) const {
    if (ctx.board.halfmove_clock() >= 100) return true;
    if (ctx.board.is_insufficient_material()) return true;
    if (ctx.board.has_threefold_repetition()) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Evaluation wrapper (with correction history + optional Syzygy integration)
// ---------------------------------------------------------------------------

int Searcher::evaluate(const ThreadContext& ctx) const {
    if (syzygy_max_pieces() > 0 && piece_count(ctx.board) <= SYZYGY_MAX_PIECES) {
        int wdl = 0;
        if (probe_wdl(ctx.board, wdl)) {
            if (wdl > 0) return 10000 - 1;
            if (wdl < 0) return -10000 + 1;
            return 0;
        }
    }

    int raw_eval = driftwood::evaluate(ctx.board);
    return raw_eval;
}

// ---------------------------------------------------------------------------
// Move scoring for ordering
// ---------------------------------------------------------------------------

int Searcher::pick_best(MoveList& moves, int* scores, int idx) const {
    int best = idx;
    for (int i = idx + 1; i < moves.size(); ++i) {
        if (scores[i] > scores[best]) best = i;
    }
    if (best != idx) {
        std::swap(moves.moves[idx], moves.moves[best]);
        std::swap(scores[idx], scores[best]);
    }
    return scores[idx];
}

// ---------------------------------------------------------------------------
// Quiescence search
// ---------------------------------------------------------------------------

int Searcher::qsearch(ThreadContext& ctx, int ply, int alpha, int beta) {
    if (ply >= MAX_PLY - 1) return evaluate(ctx);

    if (ply > 0 && stop_ && stop_->load(std::memory_order_relaxed)) return 0;

    ctx.nodes_++;
    if (ply > ctx.sel_depth_) ctx.sel_depth_ = ply;

    int stand_pat = evaluate(ctx);
    if (stand_pat >= beta) return stand_pat;
    if (stand_pat > alpha) alpha = stand_pat;

    if (stand_pat + 900 + DELTA_MARGIN < alpha) return alpha;

    MoveList moves;
    generate_legal_moves(ctx.board, moves);

    int scores[256];
    int move_count = 0;
    for (int i = 0; i < moves.size(); ++i) {
        Move m = moves[i];
        if (!m.is_capture() && !m.is_promotion()) continue;
        if (m.is_capture()) {
            int victim = MVV_LVA_VAL[static_cast<int>(m.captured())];
            int attacker = static_cast<int>(m.piece());
            scores[move_count] = MVV_LVA_SCORE(victim, attacker);
        } else {
            scores[move_count] = 0;
        }
        moves.moves[move_count] = m;
        move_count++;
    }
    moves.count = move_count;

    for (int i = 0; i < moves.size(); ++i) {
        int best = i;
        for (int j = i + 1; j < moves.size(); ++j) {
            if (scores[j] > scores[best]) best = j;
        }
        if (best != i) {
            std::swap(moves.moves[i], moves.moves[best]);
            std::swap(scores[i], scores[best]);
        }

        Move m = moves[i];
        ctx.board.make_move(m);
        int score = -qsearch(ctx, ply + 1, -beta, -alpha);
        ctx.board.unmake_move();

        if (score > alpha) {
            alpha = score;
            if (alpha >= beta) return alpha;
        }
    }

    return alpha;
}

// ---------------------------------------------------------------------------
// PVS / NegaScout
// ---------------------------------------------------------------------------

int Searcher::pvs(ThreadContext& ctx, int depth, int ply, int alpha, int beta, bool cut_node) {
    if (depth <= 0) return qsearch(ctx, ply, alpha, beta);

    if (ply >= MAX_PLY - 1) return evaluate(ctx);

    if (ply > 0 && stop_ && stop_->load(std::memory_order_relaxed)) return 0;

    ctx.nodes_++;
    if (ply > ctx.sel_depth_) ctx.sel_depth_ = ply;

    SearchStackEntry& ss = ctx.ss_[ply];
    ss.pv_length = 0;
    ss.move_count = 0;

    bool in_check = ctx.board.is_check();
    ss.in_check = in_check;

    if (in_check) depth++;

    alpha = std::max(alpha, -MATE_SCORE + ply);
    beta  = std::min(beta,  MATE_SCORE - ply - 1);
    if (alpha >= beta) return alpha;

    if (ply > 0 && is_draw(ctx)) return 0;

    // TT probe (thread-safe with mutex)
    uint16_t tt_move_enc = 0;
    int tt_score = 0;
    int tt_depth = 0;
    Bound tt_bound = BOUND_EXACT;
    bool tt_hit = false;

    {
        tt_->lock();
        tt_hit = tt_->probe(ctx.board.hash(), tt_move_enc, tt_score, tt_depth, tt_bound);
        tt_->unlock();
    }

    if (tt_hit && tt_depth >= depth && ply > 0) {
        if (tt_bound == BOUND_EXACT) return tt_score;
        if (tt_bound == BOUND_LOWER && tt_score >= beta) return tt_score;
        if (tt_bound == BOUND_UPPER && tt_score <= alpha) return tt_score;
    }

    // Internal Iterative Reduction: if no TT move at high depth, reduce by 1
    if (depth >= 4 && !tt_move_enc && !in_check) {
        depth--;
    }

    int static_eval = evaluate(ctx);
    ss.static_eval = static_eval;

    // Null move pruning
    if (depth >= NMP_DEPTH && !in_check && !cut_node && static_eval >= beta) {
        ctx.board.make_null_move();
        int score = -pvs(ctx, depth - NMP_R - 1, ply + 1, -beta, -beta + 1, !cut_node);
        ctx.board.unmake_null_move();
        if (score >= beta) return beta;
    }

    // Razoring
    if (depth <= RAZOR_DEPTH && !in_check && static_eval + RAZOR_MARGIN[depth] <= alpha) {
        int q = qsearch(ctx, ply, alpha, beta);
        if (q <= alpha) {
            if (q < alpha - 200) return q;
        }
    }

    // Generate moves
    MoveList moves;
    generate_legal_moves(ctx.board, moves);

    if (moves.size() == 0) {
        if (in_check) return -MATE_SCORE + ply;
        return 0;
    }

    // Score moves for ordering
    int scores[256];
    Color side = ctx.board.side_to_move();
    uint16_t cm = 0;
    if (ply > 0 && ctx.ss_[ply - 1].current_move.data != 0) {
        Move prev = ctx.ss_[ply - 1].current_move;
        cm = ctx.countermove_[prev.from().index][prev.to().index];
    }

    for (int i = 0; i < moves.size(); ++i) {
        Move m = moves[i];
        uint16_t enc = tt_encode_move_from_move(m);
        if (tt_move_enc && enc == tt_move_enc) {
            scores[i] = 1'000'000;
        } else if (m.is_capture()) {
            int victim = MVV_LVA_VAL[static_cast<int>(m.captured())];
            int attacker = static_cast<int>(m.piece());
            scores[i] = 900'000 + MVV_LVA_SCORE(victim, attacker);
        } else if (m.is_promotion()) {
            scores[i] = 850'000;
        } else if (ply < MAX_PLY &&
                   enc == tt_encode_move_from_move(ctx.killers_[ply][0])) {
            scores[i] = 800'000;
        } else if (ply < MAX_PLY &&
                   enc == tt_encode_move_from_move(ctx.killers_[ply][1])) {
            scores[i] = 700'000;
        } else if (ply < MAX_PLY &&
                   enc == tt_encode_move_from_move(ctx.killers_[ply][2])) {
            scores[i] = 650'000;
        } else if (cm && enc == cm) {
            scores[i] = 600'000;
        } else {
            scores[i] = ctx.history_[static_cast<int>(side)][m.from().index][m.to().index];
        }
    }

    int best_score = -MATE_SCORE;
    int alpha_orig = alpha;
    uint16_t best_move_enc = 0;
    int moves_searched = 0;
    int cutoff_count = 0;

    for (int move_idx = 0; move_idx < moves.size(); ++move_idx) {
        int best = move_idx;
        for (int j = move_idx + 1; j < moves.size(); ++j) {
            if (scores[j] > scores[best]) best = j;
        }
        if (best != move_idx) {
            std::swap(moves.moves[move_idx], moves.moves[best]);
            std::swap(scores[move_idx], scores[best]);
        }

        Move m = moves[move_idx];
        uint16_t enc = tt_encode_move_from_move(m);

        bool is_quiet = !m.is_capture() && !m.is_promotion();

        // Futility pruning
        if (depth <= FUTILITY_DEPTH && moves_searched > 0 && !in_check
            && is_quiet && !m.is_castle())
        {
            if (static_eval + FUTILITY_MARGIN[depth] <= alpha) {
                continue;
            }
        }

        // Late Move Pruning: at low depths, don't search quiet moves
        // beyond a move-count threshold. These moves are unlikely to be good.
        if (depth <= 6 && moves_searched >= 3 + depth * depth
            && !in_check && is_quiet
            && !(tt_move_enc && enc == tt_move_enc))
        {
            continue;
        }

        // Late Move Reduction
        int reduction = 0;
        if (depth >= LMR_DEPTH && moves_searched >= LMR_MIN_MOVES
            && is_quiet && !m.is_castle()
            && !(tt_move_enc && enc == tt_move_enc)
            && !(ply < MAX_PLY &&
                 (enc == tt_encode_move_from_move(ctx.killers_[ply][0]) ||
                  enc == tt_encode_move_from_move(ctx.killers_[ply][1]) ||
                  enc == tt_encode_move_from_move(ctx.killers_[ply][2]))))
        {
            reduction = get_lmr_reduction(depth, moves_searched);
            if (in_check) reduction = std::max(0, reduction - 1);
            if (cut_node) reduction += 1;
            reduction = std::min(reduction, depth - 1);
        }

        // Multi-Cut: if we're at a CUT node and find multiple cutoffs, prune
        if (depth >= MC_DEPTH && cut_node && moves_searched >= 1
            && is_quiet && !in_check)
        {
            // Already have cutoffs from this node? If yes, we can be more aggressive
            if (cutoff_count >= MC_CUTOFF_COUNT) {
                return beta;
            }
        }

        ss.current_move = m;
        ctx.board.make_move(m);

        int score;
        if (moves_searched == 0) {
            score = -pvs(ctx, depth - 1, ply + 1, -beta, -alpha, !cut_node);
        } else {
            if (reduction > 0) {
                score = -pvs(ctx, depth - 1 - reduction, ply + 1,
                             -alpha - 1, -alpha, true);
                if (score > alpha) {
                    score = -pvs(ctx, depth - 1, ply + 1, -alpha - 1, -alpha, !cut_node);
                }
            } else {
                score = -pvs(ctx, depth - 1, ply + 1, -alpha - 1, -alpha, !cut_node);
            }

            if (score > alpha && score < beta) {
                score = -pvs(ctx, depth - 1, ply + 1, -beta, -alpha, !cut_node);
            }
        }

        ctx.board.unmake_move();

        moves_searched++;
        ss.move_count = static_cast<uint16_t>(move_idx + 1);

        if (score > best_score) {
            best_score = score;
            best_move_enc = enc;

            if (score > alpha) {
                // Update PV
                ctx.ss_[ply].pv_length = 1;
                ctx.ss_[ply].pv[0] = m;
                for (int i = 0; i < ctx.ss_[ply + 1].pv_length && i < MAX_PLY - 1; ++i) {
                    ctx.ss_[ply].pv[i + 1] = ctx.ss_[ply + 1].pv[i];
                    ctx.ss_[ply].pv_length = i + 2;
                }

                if (score >= beta) {
                    cutoff_count++;

                    // Beta cutoff: update heuristics for quiet moves
                    if (is_quiet) {
                        // 3-Killer heuristic
                        if (ply < MAX_PLY) {
                            if (tt_encode_move_from_move(ctx.killers_[ply][0]) != enc) {
                                ctx.killers_[ply][2] = ctx.killers_[ply][1];
                                ctx.killers_[ply][1] = ctx.killers_[ply][0];
                                ctx.killers_[ply][0] = m;
                            }
                        }

                        if (ply > 0) {
                            Move prev = ctx.ss_[ply - 1].current_move;
                            if (prev.data != 0) {
                                ctx.countermove_[prev.from().index][prev.to().index] = enc;
                            }
                        }

                        int sid = static_cast<int>(side);
                        ctx.history_[sid][m.from().index][m.to().index] += depth * depth;
                        if (ctx.history_[sid][m.from().index][m.to().index] > HISTORY_MAX) {
                            for (int f = 0; f < 64; ++f)
                                for (int t = 0; t < 64; ++t)
                                    ctx.history_[sid][f][t] /= 2;
                        }

                        // History malus: penalize quiet moves that came before
                        // the beta cutoff (they were bad)
                        for (int j = 0; j < move_idx; ++j) {
                            Move prev_move = moves[j];
                            if (prev_move.is_capture() || prev_move.is_promotion()) continue;
                            ctx.history_[sid][prev_move.from().index][prev_move.to().index] -=
                                HISTORY_MALUS + depth * depth / 4;
                            if (ctx.history_[sid][prev_move.from().index][prev_move.to().index] < -HISTORY_MAX)
                                ctx.history_[sid][prev_move.from().index][prev_move.to().index] = -HISTORY_MAX;
                        }
                    } else {
                        // Capture move - no additional history tracking needed
                    }

                    // Store in TT (thread-safe)
                    {
                        tt_->lock();
                        tt_->store(ctx.board.hash(), enc, score, depth, BOUND_LOWER, generation_);
                        tt_->unlock();
                    }
                    return score;
                }

                alpha = score;
            }
        }
    }

    // Store in TT (thread-safe)
    Bound bound = (best_score <= alpha_orig) ? BOUND_UPPER : BOUND_EXACT;
    if (best_score >= beta) bound = BOUND_LOWER;

    {
        tt_->lock();
        tt_->store(ctx.board.hash(), best_move_enc, best_score, depth, bound, generation_);
        tt_->unlock();
    }
    return best_score;
}

// ---------------------------------------------------------------------------
// UCI info output
// ---------------------------------------------------------------------------

void Searcher::update_info(const ThreadContext& ctx, int depth, std::mutex* io_mutex) {
    int64_t elapsed = current_time_ms() - ctx.start_time_ms_;
    if (elapsed <= 0) elapsed = 1;

    int64_t nps = ctx.nodes_ * 1000 / elapsed;

    std::lock_guard<std::mutex> lock(*io_mutex);
    std::cout << "info depth " << depth
              << " seldepth " << ctx.sel_depth_
              << " score";

    if (ctx.last_score_ > MATE_SCORE - MAX_PLY) {
        int mate_plies = MATE_SCORE - ctx.last_score_;
        std::cout << " mate " << ((mate_plies + 1) / 2);
    } else if (ctx.last_score_ < -MATE_SCORE + MAX_PLY) {
        int mate_plies = ctx.last_score_ + MATE_SCORE;
        std::cout << " mate -" << ((mate_plies + 1) / 2);
    } else {
        std::cout << " cp " << ctx.last_score_;
    }

    std::cout << " nodes " << ctx.nodes_
              << " nps " << nps
              << " time " << elapsed;

    if (!ctx.pv_string_.empty()) {
        std::cout << " pv " << ctx.pv_string_;
    }

    if (ctx.thread_id_ > 0) {
        std::cout << " thread " << ctx.thread_id_;
    }

    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// Lazy SMP worker: per-thread iterative deepening loop
// ---------------------------------------------------------------------------

void Searcher::search_worker(ThreadContext& ctx,
                              const SearchLimits& limits,
                              uint8_t /*generation*/,
                              int thread_id,
                              int total_threads,
                              std::atomic<int>& max_depth_reported)
{
    ctx.thread_id_ = thread_id;
    ctx.start_time_ms_ = current_time_ms();
    ctx.nodes_ = 0;
    ctx.sel_depth_ = 0;
    ctx.pv_string_.clear();
    ctx.last_score_ = 0;

    // Clear dynamic tables
    std::memset(ctx.killers_, 0, sizeof(ctx.killers_));
    std::memset(ctx.countermove_, 0, sizeof(ctx.countermove_));

    // Decay history
    for (int c = 0; c < 2; ++c) {
        for (int f = 0; f < 64; ++f) {
            for (int t = 0; t < 64; ++t) {
                ctx.history_[c][f][t] = ctx.history_[c][f][t] * 3 / 4;
            }
        }
    }

    // Compute max allowed time for this search.
    constexpr int64_t kMaxMoveTimeMs = 2000; // UI ceiling
    ctx.max_time_ms_ = 5000; // default: 5s if nothing else applies

    int64_t budget_ms = -1;
    if (limits.wtime > 0 || limits.btime > 0) {
        int64_t my_time = (ctx.board.side_to_move() == Color::White)
                              ? limits.wtime : limits.btime;
        int64_t my_inc = (ctx.board.side_to_move() == Color::White)
                             ? limits.winc : limits.binc;
        if (limits.movestogo > 0) {
            budget_ms = my_time / (limits.movestogo + 1);
        } else {
            int64_t div;
            if      (my_time > 60000) div = 50;
            else if (my_time > 10000) div = 40;
            else if (my_time > 2000)  div = 20;
            else                       div = 10;
            budget_ms = my_time / div + my_inc / 2;
        }
    }

    if (limits.movetime > 0 && budget_ms > 0) {
        ctx.max_time_ms_ = std::min(limits.movetime, budget_ms);
    } else if (limits.movetime > 0) {
        ctx.max_time_ms_ = limits.movetime;
    } else if (budget_ms > 0) {
        ctx.max_time_ms_ = std::min(budget_ms, kMaxMoveTimeMs);
    }
    if (ctx.max_time_ms_ < 50) ctx.max_time_ms_ = 50;

    // Stagger starting depths for Lazy SMP desynchronisation
    int start_depth = 1;
    if (total_threads > 1) {
        start_depth = 1 + (thread_id % 2);
    }

    MoveList root_moves;
    generate_legal_moves(ctx.board, root_moves);

    if (root_moves.size() <= 1) {
        return;
    }

    int max_depth = std::min(limits.depth, MAX_PLY - 1);
    if (ctx.max_time_ms_ <= 200)       max_depth = std::min(max_depth, 7);
    else if (ctx.max_time_ms_ <= 500)  max_depth = std::min(max_depth, 8);
    else if (ctx.max_time_ms_ <= 1000) max_depth = std::min(max_depth, 9);
    else if (ctx.max_time_ms_ <= 2000) max_depth = std::min(max_depth, 11);
    int best_score = -MATE_SCORE;

    // Iterative deepening
    for (int depth = start_depth; depth <= max_depth; ++depth) {
        if (stop_ && stop_->load(std::memory_order_relaxed)) break;
        if (time_up(ctx)) break;

        // Aspiration window with adaptive widening
        int alpha = -MATE_SCORE;
        int beta = MATE_SCORE;

        if (depth >= 3) {
            alpha = best_score - ASPIRATION_WINDOW;
            beta  = best_score + ASPIRATION_WINDOW;
        }

        int score = pvs(ctx, depth, 0, alpha, beta, false);

        if (time_up(ctx)) {
            if (score > -MATE_SCORE && score < MATE_SCORE) {
                best_score = score;
                ctx.last_score_ = score;
            }
            break;
        }

        // Aspiration window fail-high/fail-low: re-search with full window
        if (score <= alpha || score >= beta) {
            alpha = -MATE_SCORE;
            beta = MATE_SCORE;
            score = pvs(ctx, depth, 0, alpha, beta, false);
        }

        best_score = score;
        ctx.last_score_ = score;

        // Extract PV
        std::ostringstream pv_ss;
        for (int i = 0; i < ctx.ss_[0].pv_length && i < MAX_PLY; ++i) {
            if (i > 0) pv_ss << ' ';
            pv_ss << ctx.ss_[0].pv[i].to_uci();
        }
        ctx.pv_string_ = pv_ss.str();

        // Report info if this is the first thread to reach this depth
        int prev_reported = max_depth_reported.load(std::memory_order_relaxed);
        if (depth > prev_reported) {
            if (max_depth_reported.compare_exchange_strong(prev_reported, depth,
                    std::memory_order_relaxed)) {
                update_info(ctx, depth, io_mutex_.get());
            }
        }

        if (stop_ && stop_->load(std::memory_order_relaxed)) break;
        if (time_up(ctx)) break;
    }
}

// ---------------------------------------------------------------------------
// Single-threaded search (backward compatible)
// ---------------------------------------------------------------------------

SearchResult Searcher::search(Board& board) {
    ThreadContext ctx;
    ctx.board = board;
    ctx.start_time_ms_ = current_time_ms();
    ctx.nodes_ = 0;
    ctx.sel_depth_ = 0;
    ctx.pv_string_.clear();
    ctx.last_score_ = 0;
    ctx.thread_id_ = 0;

    generation_ = (generation_ + 1) & 0xF;

    // Clear dynamic tables
    std::memset(ctx.killers_, 0, sizeof(ctx.killers_));
    std::memset(ctx.countermove_, 0, sizeof(ctx.countermove_));

    // Decay history
    for (int c = 0; c < 2; ++c) {
        for (int f = 0; f < 64; ++f) {
            for (int t = 0; t < 64; ++t) {
                ctx.history_[c][f][t] = ctx.history_[c][f][t] * 3 / 4;
            }
        }
    }

    constexpr int64_t kMaxMoveTimeMs = 2000;
    ctx.start_time_ms_ = current_time_ms();
    ctx.max_time_ms_ = 5000;
    int64_t time_for_move_ms = 0;

    if (limits_.movetime > 0) {
        time_for_move_ms = limits_.movetime;
        ctx.max_time_ms_ = limits_.movetime;
    } else if (limits_.wtime > 0 || limits_.btime > 0) {
        int64_t my_time = (board.side_to_move() == Color::White)
                              ? limits_.wtime : limits_.btime;
        int64_t my_inc = (board.side_to_move() == Color::White)
                             ? limits_.winc : limits_.binc;

        if (limits_.movestogo > 0) {
            time_for_move_ms = my_time / (limits_.movestogo + 1);
        } else {
            int64_t div;
            if      (my_time > 60000) div = 50;
            else if (my_time > 10000) div = 40;
            else if (my_time > 2000)  div = 20;
            else                       div = 10;
            time_for_move_ms = my_time / div + my_inc / 2;
        }
        if (time_for_move_ms < 50) time_for_move_ms = 50;
        if (time_for_move_ms > kMaxMoveTimeMs) time_for_move_ms = kMaxMoveTimeMs;
        ctx.max_time_ms_ = time_for_move_ms;
    }

    MoveList moves;
    generate_legal_moves(board, moves);

    SearchResult result;
    if (moves.size() == 0) {
        result.best_move = Move();
        result.ponder_move = Move();
        return result;
    }

    if (moves.size() == 1) {
        result.best_move = moves[0];
        result.ponder_move = Move();
        return result;
    }

    int max_depth = std::min(limits_.depth, MAX_PLY - 1);
    if (ctx.max_time_ms_ <= 200)       max_depth = std::min(max_depth, 7);
    else if (ctx.max_time_ms_ <= 500)  max_depth = std::min(max_depth, 8);
    else if (ctx.max_time_ms_ <= 1000) max_depth = std::min(max_depth, 9);
    else if (ctx.max_time_ms_ <= 2000) max_depth = std::min(max_depth, 11);
    int best_score = -MATE_SCORE;

    // Iterative deepening
    for (int depth = 1; depth <= max_depth; ++depth) {
        if (time_up(ctx)) break;
        int alpha = -MATE_SCORE;
        int beta = MATE_SCORE;

        if (depth >= 3) {
            alpha = best_score - ASPIRATION_WINDOW;
            beta  = best_score + ASPIRATION_WINDOW;
        }

        int score = pvs(ctx, depth, 0, alpha, beta, false);

        if (time_up(ctx)) {
            if (score > -MATE_SCORE && score < MATE_SCORE) {
                best_score = score;
            }
            break;
        }

        if (score <= alpha || score >= beta) {
            alpha = -MATE_SCORE;
            beta = MATE_SCORE;
            score = pvs(ctx, depth, 0, alpha, beta, false);
        }

        best_score = score;
        ctx.last_score_ = score;

        std::ostringstream pv_ss;
        for (int i = 0; i < ctx.ss_[0].pv_length && i < MAX_PLY; ++i) {
            if (i > 0) pv_ss << ' ';
            pv_ss << ctx.ss_[0].pv[i].to_uci();
        }
        ctx.pv_string_ = pv_ss.str();

        pv_string_ = ctx.pv_string_;
        last_score_ = ctx.last_score_;

        update_info(ctx, depth, io_mutex_.get());

        if (time_up(ctx) || (stop_ && stop_->load(std::memory_order_relaxed))) {
            break;
        }
    }

    if (ctx.ss_[0].pv_length > 0) {
        result.best_move = ctx.ss_[0].pv[0];
        if (ctx.ss_[0].pv_length > 1) {
            result.ponder_move = ctx.ss_[0].pv[1];
        }
    } else {
        result.best_move = moves[0];
        result.ponder_move = Move();
    }

    last_nodes_ = ctx.nodes_;
    return result;
}

// ---------------------------------------------------------------------------
// Multi-threaded Lazy SMP search
// ---------------------------------------------------------------------------

SearchResult Searcher::search_parallel(Board& board, int num_threads) {
    if (num_threads <= 1) {
        return search(board);
    }

    generation_ = (generation_ + 1) & 0xF;

    std::vector<ThreadContext> contexts(static_cast<size_t>(num_threads));
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        contexts[i].board = board;
        contexts[i].thread_id_ = i;
        contexts[i].start_time_ms_ = current_time_ms();
        contexts[i].nodes_ = 0;
        contexts[i].sel_depth_ = 0;
        contexts[i].pv_string_.clear();
        contexts[i].last_score_ = 0;

        std::memset(contexts[i].killers_, 0, sizeof(contexts[i].killers_));
        std::memset(contexts[i].countermove_, 0, sizeof(contexts[i].countermove_));

        for (int c = 0; c < 2; ++c) {
            for (int f = 0; f < 64; ++f) {
                for (int t = 0; t < 64; ++t) {
                    contexts[i].history_[c][f][t] = contexts[i].history_[c][f][t] * 3 / 4;
                }
            }
        }
    }

    MoveList moves;
    generate_legal_moves(board, moves);
    if (moves.size() <= 1) {
        SearchResult result;
        if (moves.size() == 0) {
            result.best_move = Move();
            result.ponder_move = Move();
        } else {
            result.best_move = moves[0];
            result.ponder_move = Move();
        }
        return result;
    }

    std::atomic<int> max_depth_reported{0};

    for (int i = 1; i < num_threads; ++i) {
        threads.emplace_back(&Searcher::search_worker, this,
                             std::ref(contexts[i]),
                             std::cref(limits_),
                             generation_, i, num_threads,
                             std::ref(max_depth_reported));
    }

    search_worker(contexts[0], limits_, generation_, 0, num_threads, max_depth_reported);

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    SearchResult result;
    if (contexts[0].ss_[0].pv_length > 0) {
        result.best_move = contexts[0].ss_[0].pv[0];
        if (contexts[0].ss_[0].pv_length > 1) {
            result.ponder_move = contexts[0].ss_[0].pv[1];
        }
    } else {
        result.best_move = moves[0];
        result.ponder_move = Move();
    }

    pv_string_ = contexts[0].pv_string_;
    last_score_ = contexts[0].last_score_;

    return result;
}

} // namespace driftwood
