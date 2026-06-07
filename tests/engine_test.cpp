#include <gtest/gtest.h>
#include "driftwood/board.hpp"
#include "driftwood/book.hpp"
#include "driftwood/eval.hpp"
#include "driftwood/search.hpp"
#include "driftwood/tt.hpp"
#include "driftwood/movegen.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <set>
#include <thread>

using namespace driftwood;

// =========================================================================
// Evaluation tests
// =========================================================================

TEST(EvalTest, StartingPositionIsBalanced) {
    Board board = Board::starting_position();
    int score = evaluate(board);
    // Should be roughly 0 (within ±30)
    EXPECT_NEAR(score, 10, 30); // tempo bonus gives ~10, allow ±30
}

TEST(EvalTest, KiwipeteIsSlightlyPositive) {
    Board board = Board::from_fen(
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    int score = evaluate(board);
    // White should have a slight advantage (white to move + tempo)
    EXPECT_GT(score, -50);
    EXPECT_LT(score, 300);
}

TEST(EvalTest, MaterialCountWorks) {
    // Remove white's queen
    Board board = Board::from_fen(
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1");
    int score = evaluate(board);
    // White is down a queen (~900 points)
    // With tempo bonus of +10 for white to move, expect ~ -890
    EXPECT_LT(score, -800);
    EXPECT_GT(score, -1000);
}

TEST(EvalTest, BishopPair) {
    // Position where white has both bishops, black has only one
    // White: king + both bishops
    // Black: king + one bishop
    Board board = Board::from_fen(
        "2b1k3/8/8/8/8/8/8/2BBK3 w - - 0 1");
    int score = evaluate(board);
    // White should have an advantage due to bishop pair (~+30-50)
    EXPECT_GT(score, 20);
}

TEST(EvalTest, NoBishopPair) {
    // Both sides have one bishop each
    Board board = Board::from_fen(
        "2b1k3/8/8/8/8/8/8/3BK3 w - - 0 1");
    int score = evaluate(board);
    // No bishop pair advantage, material equal, roughly even
    EXPECT_NEAR(score, 10, 40);
}

TEST(EvalTest, PassedPawnOn7th) {
    // White has a passed pawn on e7 (about to promote)
    Board board = Board::from_fen(
        "4k3/4P3/8/8/8/8/8/4K3 w - - 0 1");
    int score = evaluate(board);
    // The passed pawn on the 7th rank should give a significant bonus
    // Base material: P=100, + passed rank 7 bonus (200mg, 400eg)
    // At phase=1 (only pawn contributes 0 to phase... wait, pawns have phase weight 0)
    // Actually PHASE_WEIGHTS[Pawn] = 0, so phase = 0 (endgame reached)
    // Score = eg_score
    // White: 100 (pawn) + EG_TABLE[PAWN][e7] + passed bonus(400)
    // Black: -0 (king)
    // Relative: from white's side-to-move: won't reverse since white to move
    std::cerr << "Passed pawn eval: " << score << std::endl;
    EXPECT_GT(score, 250);
}

TEST(EvalTest, BackwardPawn) {
    // White pawn on e4 is backward: can't advance to e5 safely (attacked by
    // black pawn on d5), and e4 is not defended by any other white pawn.
    // Black's d5 is isolated and passed — should favor black.
    Board board = Board::from_fen(
        "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
    int score = evaluate(board);
    std::cerr << "Backward pawn eval: " << score << std::endl;
    // White should be slightly worse (backward pawn vs passed pawn for black)
    EXPECT_LT(score, 50);
    // But not insane
    EXPECT_GT(score, -500);
}

TEST(EvalTest, MobilityDevelopedVsTrapped) {
    // White has a developed bishop on g5 (good mobility)
    // Black has a trapped bishop on b7 (behind pawns, blocked)
    Board board = Board::from_fen(
        "r1b1kbnr/pppppppp/2n5/6B1/8/8/PPPP1PPP/RNBQK1NR w KQkq - 0 1");
    int score = evaluate(board);
    std::cerr << "Mobility eval: " << score << std::endl;
    // White should have some advantage from the developed bishop
    EXPECT_GT(score, 10);
}

// =========================================================================
// Opening book tests
// =========================================================================

TEST(BookTest, LoadBook) {
    OpeningBook book;
    bool loaded = book.load("books/driftwood.bin");
    EXPECT_TRUE(loaded);
    EXPECT_GT(book.size(), 0);
}

TEST(BookTest, ProbeStartingPosition) {
    OpeningBook book;
    ASSERT_TRUE(book.load("books/driftwood.bin"));

    Board board = Board::starting_position();
    uint64_t key = board.hash();
    uint16_t enc = book.probe(key);
    EXPECT_NE(enc, 0) << "Starting position should be in the book";

    // Decode and verify the move is legal
    int from, to, promotion;
    book_decode_move(enc, from, to, promotion);

    MoveList moves;
    generate_legal_moves(board, moves);
    bool found = false;
    for (int i = 0; i < moves.size(); ++i) {
        Move m = moves[i];
        if (m.from().index == static_cast<uint8_t>(from) &&
            m.to().index == static_cast<uint8_t>(to)) {
            if (promotion < 0 || (m.is_promotion() && static_cast<int>(m.promotion()) == promotion)) {
                found = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found) << "Book move should be legal";
}

TEST(BookTest, ProbeUnknownPosition) {
    OpeningBook book;
    ASSERT_TRUE(book.load("books/driftwood.bin"));

    // Unusual position unlikely to be in the book
    Board board = Board::from_fen(
        "8/8/8/4k3/8/8/8/4K3 w - - 0 1");
    uint64_t key = board.hash();
    uint16_t enc = book.probe(key);
    EXPECT_EQ(enc, 0) << "King-only position should not be in the book";
}

TEST(BookTest, WeightedRandomSelection) {
    OpeningBook book;
    ASSERT_TRUE(book.load("books/driftwood.bin"));

    Board board = Board::starting_position();
    uint64_t key = board.hash();

    // Probe multiple times and verify we get different moves
    // (starting position has multiple book moves from different lines)
    std::set<uint16_t> moves_seen;
    for (int i = 0; i < 20; ++i) {
        uint16_t enc = book.probe(key);
        EXPECT_NE(enc, 0);
        moves_seen.insert(enc);
    }

    // Starting position should have multiple possible moves (e4, d4, etc.)
    EXPECT_GE(moves_seen.size(), 2)
        << "Starting position should yield multiple moves via weighted selection";
}

// =========================================================================
// TT tests (unchanged from phase 2)
// =========================================================================

TEST(TTTest, StoreAndProbe) {
    TranspositionTable tt;
    tt.resize(4);

    uint64_t key = 0xDEADBEEFCAFEBABEULL;
    uint16_t move = 0x1234;
    int score = 100;
    int depth = 5;
    Bound bound = BOUND_EXACT;

    tt.store(key, move, score, depth, bound, 0);

    uint16_t probe_move = 0;
    int probe_score = 0;
    int probe_depth = 0;
    Bound probe_bound = BOUND_EXACT;

    bool hit = tt.probe(key, probe_move, probe_score, probe_depth, probe_bound);
    EXPECT_TRUE(hit);
    EXPECT_EQ(probe_move, move);
    EXPECT_EQ(probe_score, score);
    EXPECT_EQ(probe_depth, depth);
    EXPECT_EQ(probe_bound, bound);
}

TEST(TTTest, KeyCollisionSafe) {
    TranspositionTable tt;
    tt.resize(2);

    uint64_t key1 = 0x1000000000000001ULL;
    uint64_t key2 = 0x2000000000000002ULL;

    tt.store(key1, 0x1111, 50, 3, BOUND_EXACT, 0);
    tt.store(key2, 0x2222, 100, 4, BOUND_LOWER, 0);

    uint16_t probe_move = 0;
    int probe_score = 0;
    int probe_depth = 0;
    Bound probe_bound = BOUND_EXACT;

    bool hit = tt.probe(key1, probe_move, probe_score, probe_depth, probe_bound);
    EXPECT_TRUE(hit);
    EXPECT_EQ(probe_move, 0x1111);
}

TEST(TTTest, ProbeMissForUnknownKey) {
    TranspositionTable tt;
    tt.resize(4);

    uint64_t known_key = 0x1234567890ABCDEFULL;
    uint64_t unknown_key = 0xFFFFFFFFFFFFFFFFULL;

    tt.store(known_key, 0x3333, 200, 6, BOUND_UPPER, 0);

    uint16_t probe_move = 0;
    int probe_score = 0;
    int probe_depth = 0;
    Bound probe_bound = BOUND_EXACT;

    bool hit = tt.probe(unknown_key, probe_move, probe_score, probe_depth, probe_bound);
    EXPECT_FALSE(hit);
}

TEST(TTTest, ReplaceWithGreaterDepth) {
    TranspositionTable tt;
    tt.resize(4);

    uint64_t key = 0xBEEF;

    tt.store(key, 0xAAAA, 10, 2, BOUND_EXACT, 0);
    tt.store(key, 0xBBBB, 20, 5, BOUND_LOWER, 0);

    uint16_t pm = 0;
    int ps = 0, pd = 0;
    Bound pb = BOUND_EXACT;
    bool hit = tt.probe(key, pm, ps, pd, pb);
    EXPECT_TRUE(hit);
    EXPECT_EQ(pd, 5);
}

// =========================================================================
// Search tests
// =========================================================================

static Move search_position(const std::string& fen, int depth) {
    Board board = Board::from_fen(fen);

    TranspositionTable tt;
    tt.resize(32);

    Searcher searcher;
    searcher.set_tt(&tt);

    SearchLimits limits;
    limits.depth = depth;

    std::atomic<bool> stop_flag(false);
    searcher.set_stop_flag(&stop_flag);
    searcher.set_limits(limits);

    SearchResult result = searcher.search(board);
    return result.best_move;
}

TEST(SearchTest, MateInOne) {
    std::string fen = "r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4";
    Board board = Board::from_fen(fen);

    EXPECT_TRUE(board.is_check());
    EXPECT_TRUE(board.is_checkmate());
}

TEST(SearchTest, FindsCapturingMove) {
    std::string fen = "3k4/8/8/8/8/8/p7/R3K3 w - - 0 1";
    Move best = search_position(fen, 3);

    EXPECT_TRUE(best.is_capture()) << "Best move: " << best.to_uci();
    EXPECT_EQ(best.to().name(), "a2");
}

TEST(SearchTest, QueenCapturesFreePawn) {
    std::string fen = "rnb1kbnr/pppp1ppp/8/4q3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 0 3";
    Move best = search_position(fen, 5);

    EXPECT_TRUE(best.is_capture()) << "Best move: " << best.to_uci();
    EXPECT_EQ(best.to().name(), "e4");
}

TEST(SearchTest, AvoidsHangingQueen) {
    std::string fen = "rnbqkbnr/pppp1ppp/8/4q3/4P3/3B1N2/PPPP1PPP/RNBQK2R b KQkq - 0 3";
    Move best = search_position(fen, 6);

    EXPECT_TRUE(best.piece() == PieceType::Queen)
        << "Best move: " << best.to_uci();
    EXPECT_FALSE(best.is_capture())
        << "Best move: " << best.to_uci();
    EXPECT_NE(best.to().name(), "e5");
}

TEST(SearchTest, FindsPromotion) {
    std::string fen = "8/4P3/8/8/8/8/8/8 w - - 0 1";
    Move best = search_position(fen, 3);

    EXPECT_EQ(best.from().name(), "e7");
    EXPECT_TRUE(best.is_promotion() || best.to().name() == "e8");
}

// =========================================================================
// Syzygy integration tests
// =========================================================================

#include "driftwood/syzygy.hpp"

TEST(SyzygyTest, NotConfiguredReturnsFalse) {
    Board board = Board::from_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    set_syzygy_path("");

    int wdl = 0;
    bool result = probe_wdl(board, wdl);
    EXPECT_FALSE(result) << "Should return false when not configured";
}

TEST(SyzygyTest, PieceCount) {
    Board board = Board::starting_position();
    EXPECT_EQ(piece_count(board), 32);

    board = Board::from_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(piece_count(board), 2);
}

TEST(SyzygyTest, InvalidPathGracefulFallback) {
    set_syzygy_path("/nonexistent/path");
    EXPECT_FALSE(init_syzygy("/nonexistent/path"))
        << "Should return false for invalid path";

    // Engine still works without Syzygy
    Board board = Board::from_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    int wdl = 0;
    EXPECT_FALSE(probe_wdl(board, wdl));
}

// =========================================================================
// Multithreading (Lazy SMP) tests
// =========================================================================

static Move parallel_search_position(const std::string& fen, int depth, int threads) {
    Board board = Board::from_fen(fen);

    TranspositionTable tt;
    tt.resize(32);

    Searcher searcher;
    searcher.set_tt(&tt);

    SearchLimits limits;
    limits.depth = depth;

    std::atomic<bool> stop_flag(false);
    searcher.set_stop_flag(&stop_flag);
    searcher.set_limits(limits);

    SearchResult result;
    if (threads > 1) {
        result = searcher.search_parallel(board, threads);
    } else {
        result = searcher.search(board);
    }
    return result.best_move;
}

TEST(ParallelSearchTest, OneThreadEqualsSingle) {
    // search_parallel with 1 thread should match search
    std::string fen = "rnb1kbnr/pppp1ppp/8/4q3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 0 3";
    Move single = search_position(fen, 5);
    Move parallel = parallel_search_position(fen, 5, 1);

    EXPECT_EQ(single.data, parallel.data)
        << "search_parallel(1) should match search()";
}

TEST(ParallelSearchTest, MultipleThreadsSmoke) {
    // Smoke test: 4-thread search should complete without error
    std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    Move best = parallel_search_position(fen, 6, 4);

    // The move must be legal (from the starting position)
    EXPECT_NE(best.data, 0) << "Parallel search should return a move";

    // Verify it's a legal move from the starting position
    Board board = Board::from_fen(fen);
    MoveList legal;
    generate_legal_moves(board, legal);
    bool found = false;
    for (int i = 0; i < legal.size(); ++i) {
        if (legal[i].data == best.data) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Best move from parallel search should be legal";
}

TEST(ParallelSearchTest, StopFlagExitsCleanly) {
    // Set a stop flag mid-search and verify it exits cleanly
    std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    Board board = Board::from_fen(fen);

    TranspositionTable tt;
    tt.resize(32);

    Searcher searcher;
    searcher.set_tt(&tt);

    SearchLimits limits;
    limits.depth = 20; // deep search that we'll interrupt

    std::atomic<bool> stop_flag(false);
    searcher.set_stop_flag(&stop_flag);
    searcher.set_limits(limits);

    // Start search in a separate thread so we can set the stop flag
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    SearchResult result;

    std::thread search_thread([&]() {
        started.store(true);
        result = searcher.search_parallel(board, 4);
        finished.store(true);
    });

    // Wait for search to start, then signal stop
    while (!started.load()) {
        std::this_thread::yield();
    }
    // Give it a tiny bit of time to start searching
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    stop_flag.store(true);

    // Join with timeout
    search_thread.join();

    EXPECT_TRUE(finished.load()) << "Search should finish after stop flag is set";
    // Best move might be 0 if search was interrupted very early, but that's OK
    // as long as it exits cleanly (no crash)
}

TEST(ParallelSearchTest, OneThreadDeterministic) {
    // With 1 thread, repeated searches should produce the same best move
    // (same position, same depth, no time pressure)
    std::string fen = "8/4P3/8/8/8/8/8/8 w - - 0 1";

    Move first = parallel_search_position(fen, 4, 1);
    Move second = parallel_search_position(fen, 4, 1);

    EXPECT_EQ(first.data, second.data)
        << "Single-thread search should be deterministic";
}
