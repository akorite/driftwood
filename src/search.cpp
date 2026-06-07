// ---------------------------------------------------------------------------
// DriftWood Chess Engine — Lazy SMP Search
//
// Phase 4 adds Lazy SMP multithreading. The design follows the standard
// Lazy SMP approach described by Daniel Uranga and used in Stockfish:
// multiple threads share a single transposition table and run iterative
// deepening independently. Each thread has its own history, killer,
// countermove, and search stack tables. The TT acts as the sole
// communication channel: when one thread finds a beta cutoff, the
// result is stored in the TT and picked up by other threads at their
// next node. Threads are slightly desynchronised by starting at
// staggered depths, which increases the chance they explore different
// parts of the tree first.
//
// The TT is protected by a per-instance mutex (locked during probe and
// store). With the small TT entry size (8 bytes), lock contention is
// minimal in practice even with 4-8 threads.
// ---------------------------------------------------------------------------

#include "driftwood/search.hpp"
#include "driftwood/eval.hpp"
#include "driftwood/movegen.hpp"
#include "driftwood/syzygy.hpp"

#include <algorithm>
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

// Syzygy integration: use WDL as eval when pieces are few
constexpr int SYZYGY_MAX_PIECES = 6;

// Simple LMR reduction table
static int lmr_reduction(int depth, int moves_searched) {
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
    // max_time_ms is stored in the ThreadContext by the worker
    return (current_time_ms() - ctx.start_time_ms_) >= ctx.max_time_ms_;
}

bool Searcher::is_draw(const ThreadContext& ctx) const {
    if (ctx.board.halfmove_clock() >= 100) return true;
    if (ctx.board.is_insufficient_material()) return true;
    if (ctx.board.has_threefold_repetition()) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Evaluation wrapper (with optional Syzygy integration)
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
    return driftwood::evaluate(ctx.board);
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

        // Futility pruning
        if (depth <= FUTILITY_DEPTH && moves_searched > 0 && !in_check
            && !m.is_capture() && !m.is_promotion() && !m.is_castle())
        {
            if (static_eval + FUTILITY_MARGIN[depth] <= alpha) {
                continue;
            }
        }

        // Late Move Reduction
        int reduction = 0;
        if (depth >= LMR_DEPTH && moves_searched >= LMR_MIN_MOVES
            && !m.is_capture() && !m.is_promotion() && !m.is_castle()
            && !(tt_move_enc && enc == tt_move_enc)
            && !(ply < MAX_PLY &&
                 (enc == tt_encode_move_from_move(ctx.killers_[ply][0]) ||
                  enc == tt_encode_move_from_move(ctx.killers_[ply][1]))))
        {
            reduction = lmr_reduction(depth, moves_searched);
            if (in_check) reduction = std::max(0, reduction - 1);
            if (cut_node) reduction += 1;
            reduction = std::min(reduction, depth - 1);
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
                    // Beta cutoff: update heuristics for quiet moves
                    if (!m.is_capture()) {
                        if (ply < MAX_PLY) {
                            if (tt_encode_move_from_move(ctx.killers_[ply][0]) != enc) {
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
                        if (ctx.history_[sid][m.from().index][m.to().index] > 16384) {
                            for (int f = 0; f < 64; ++f)
                                for (int t = 0; t < 64; ++t)
                                    ctx.history_[sid][f][t] /= 2;
                        }
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
    // Priority: explicit movetime wins, then wtime/btime budget, with
    // movetime acting as a soft cap when both are present.
    // The wtime/btime budget is proportional to remaining time and uses
    // an aggressive divisor tuned for a casual UI (not tournament play).
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
            // Casual-play time management: tight cap, aggressive divisor.
            //   10 min : ~1.5s (capped)
            //   1 min  : ~1.2s
            //   30s    : ~600ms
            //   10s    : ~200ms
            //   < 1s   : 100ms (minimum)
            int64_t div;
            if      (my_time > 60000) div = 50; // 10+ min games
            else if (my_time > 10000) div = 40; // 10s–1min
            else if (my_time > 2000)  div = 20; // 2–10s
            else                       div = 10; // <2s — emergency
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
        // Thread 0 starts at depth 1, others at depth 1 or 2
        start_depth = 1 + (thread_id % 2);
    }

    MoveList root_moves;
    generate_legal_moves(ctx.board, root_moves);

    if (root_moves.size() <= 1) {
        return;
    }

    int max_depth = std::min(limits.depth, MAX_PLY - 1);
    // Cap depth for tight budgets so a single deep iteration can't blow
    // past the time limit (search granularity is per-depth, not per-node).
    // Branching factor is ~3x per ply, so each depth tier is ~3x slower.
    if (ctx.max_time_ms_ <= 200)       max_depth = std::min(max_depth, 7);
    else if (ctx.max_time_ms_ <= 500)  max_depth = std::min(max_depth, 8);
    else if (ctx.max_time_ms_ <= 1000) max_depth = std::min(max_depth, 9);
    else if (ctx.max_time_ms_ <= 2000) max_depth = std::min(max_depth, 11);
    int best_score = -MATE_SCORE;

    // Iterative deepening
    for (int depth = start_depth; depth <= max_depth; ++depth) {
        // Check stop flag
        if (stop_ && stop_->load(std::memory_order_relaxed)) break;
        if (time_up(ctx)) break;

        // Aspiration window for depth >= 3
        int alpha = -MATE_SCORE;
        int beta = MATE_SCORE;

        if (depth >= 3) {
            alpha = best_score - ASPIRATION_WINDOW;
            beta  = best_score + ASPIRATION_WINDOW;
        }

        int score = pvs(ctx, depth, 0, alpha, beta, false);

        // Bail immediately if the time budget is exhausted. A single deep
        // iteration can blow past the budget; we cannot wait for the next
        // depth's start-of-loop check.
        if (time_up(ctx)) {
            if (score > -MATE_SCORE && score < MATE_SCORE) {
                best_score = score;
                ctx.last_score_ = score;
            }
            break;
        }

        // Aspiration window fail-high/ fail-low: re-search with full window
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

        // Check stop flag after each depth
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

    // Time management — must match the formula in search_worker above.
    // Casual-play tuning: aggressive divisor + 1500ms UI ceiling.
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
        // Hard ceiling for casual UI
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
    // Cap depth for tight budgets so a single deep iteration can't blow
    // past the time limit (search granularity is per-depth, not per-node).
    // Branching factor is ~3x per ply, so each depth tier is ~3x slower.
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

        // Bail immediately if budget is exhausted (a single deep iteration
        // can take much longer than the time limit on its own).
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

    // Allocate per-thread contexts (no heap allocation in the hot path)
    std::vector<ThreadContext> contexts(static_cast<size_t>(num_threads));
    std::vector<std::thread> threads;

    // Initialize each context with a copy of the board
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

    // Check for single legal move
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

    // Shared atomic for depth reporting (only the first thread to reach
    // a new depth prints an info string)
    std::atomic<int> max_depth_reported{0};

    // Spawn worker threads (thread 0 runs on the calling thread)
    for (int i = 1; i < num_threads; ++i) {
        threads.emplace_back(&Searcher::search_worker, this,
                             std::ref(contexts[i]),
                             std::cref(limits_),
                             generation_, i, num_threads,
                             std::ref(max_depth_reported));
    }

    // Thread 0 runs on the main thread
    search_worker(contexts[0], limits_, generation_, 0, num_threads, max_depth_reported);

    // Join all threads
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    // Extract result from thread 0
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

    // Store last results for backward compat
    pv_string_ = contexts[0].pv_string_;
    last_score_ = contexts[0].last_score_;

    return result;
}

} // namespace driftwood
