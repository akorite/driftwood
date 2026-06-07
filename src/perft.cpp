#include "driftwood/perft.hpp"
#include "driftwood/board.hpp"
#include "driftwood/movegen.hpp"
#include <iostream>
#include <chrono>

namespace driftwood {

uint64_t perft(Board& board, int depth) {
    if (depth == 0) return 1;

    MoveList moves;
    generate_legal_moves(board, moves);

    if (depth == 1) {
        return static_cast<uint64_t>(moves.size());
    }

    uint64_t total = 0;
    for (int i = 0; i < moves.size(); i++) {
        board.make_move(moves[i]);
        total += perft(board, depth - 1);
        board.unmake_move();
    }
    return total;
}

uint64_t perft_split(Board& board, int depth) {
    MoveList moves;
    generate_legal_moves(board, moves);

    uint64_t total = 0;
    for (int i = 0; i < moves.size(); i++) {
        Move m = moves[i];
        board.make_move(m);
        uint64_t count = (depth <= 1) ? 1 : perft(board, depth - 1);
        board.unmake_move();
        std::cout << m.to_uci() << ": " << count << std::endl;
        total += count;
    }
    return total;
}

struct PerftCase {
    const char* name;
    const char* fen;
    int depth;
    uint64_t expected;
};

void run_perftsuite() {
    static const PerftCase cases[] = {
        {"Starting position",
         "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
         5, 4865609},
        {"Kiwipete",
         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         4, 4085603},
        {"Position 3",
         "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
         5, 674624},
        {"Position 4",
         "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
         4, 422333},
        {"Position 5",
         "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
         4, 2103487},
        {"Position 6",
         "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
         4, 3894594},
    };

    int passed = 0;
    int failed = 0;
    for (const auto& c : cases) {
        Board board = Board::from_fen(c.fen);
        auto start = std::chrono::steady_clock::now();
        uint64_t result = perft(board, c.depth);
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        bool is_pass = (result == c.expected);
        if (is_pass) passed++; else failed++;
        std::cout << (is_pass ? "PASS" : "FAIL")
                  << " (" << c.name << ", depth " << c.depth
                  << "): got " << result
                  << ", expected " << c.expected
                  << " [" << ms << "ms]"
                  << std::endl;
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed" << std::endl;
}

} // namespace driftwood
