#include <gtest/gtest.h>
#include "driftwood/board.hpp"
#include "driftwood/perft.hpp"

using namespace driftwood;

static void perft_verify(const std::string& fen, int depth, uint64_t expected) {
    Board board = Board::from_fen(fen);
    uint64_t result = perft(board, depth);
    ASSERT_EQ(result, expected)
        << "Perft depth " << depth << " on FEN '" << fen << "'";
}

// Starting position
TEST(PerftTest, StartPosDepth1) { perft_verify(DEFAULT_FEN, 1, 20); }
TEST(PerftTest, StartPosDepth2) { perft_verify(DEFAULT_FEN, 2, 400); }
TEST(PerftTest, StartPosDepth3) { perft_verify(DEFAULT_FEN, 3, 8902); }
TEST(PerftTest, StartPosDepth4) { perft_verify(DEFAULT_FEN, 4, 197281); }
TEST(PerftTest, StartPosDepth5) { perft_verify(DEFAULT_FEN, 5, 4865609); }

// Kiwipete
constexpr const char* KIWIPETE = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

TEST(PerftTest, KiwipeteDepth1) { perft_verify(KIWIPETE, 1, 48); }
TEST(PerftTest, KiwipeteDepth2) { perft_verify(KIWIPETE, 2, 2039); }
TEST(PerftTest, KiwipeteDepth3) { perft_verify(KIWIPETE, 3, 97862); }
TEST(PerftTest, KiwipeteDepth4) { perft_verify(KIWIPETE, 4, 4085603); }

// Position 3
constexpr const char* POS3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";

TEST(PerftTest, Pos3Depth1) { perft_verify(POS3, 1, 14); }
TEST(PerftTest, Pos3Depth2) { perft_verify(POS3, 2, 191); }
TEST(PerftTest, Pos3Depth3) { perft_verify(POS3, 3, 2812); }
TEST(PerftTest, Pos3Depth4) { perft_verify(POS3, 4, 43238); }
TEST(PerftTest, Pos3Depth5) { perft_verify(POS3, 5, 674624); }

// Position 4
constexpr const char* POS4 = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";

TEST(PerftTest, Pos4Depth1) { perft_verify(POS4, 1, 6); }
TEST(PerftTest, Pos4Depth2) { perft_verify(POS4, 2, 264); }
TEST(PerftTest, Pos4Depth3) { perft_verify(POS4, 3, 9467); }
TEST(PerftTest, Pos4Depth4) { perft_verify(POS4, 4, 422333); }

// Position 5
constexpr const char* POS5 = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";

TEST(PerftTest, Pos5Depth1) { perft_verify(POS5, 1, 44); }
TEST(PerftTest, Pos5Depth2) { perft_verify(POS5, 2, 1486); }
TEST(PerftTest, Pos5Depth3) { perft_verify(POS5, 3, 62379); }
TEST(PerftTest, Pos5Depth4) { perft_verify(POS5, 4, 2103487); }

// Position 6
constexpr const char* POS6 = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";

TEST(PerftTest, Pos6Depth1) { perft_verify(POS6, 1, 46); }
TEST(PerftTest, Pos6Depth2) { perft_verify(POS6, 2, 2079); }
TEST(PerftTest, Pos6Depth3) { perft_verify(POS6, 3, 89890); }
TEST(PerftTest, Pos6Depth4) { perft_verify(POS6, 4, 3894594); }
