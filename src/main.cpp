#include "driftwood/board.hpp"
#include "driftwood/book.hpp"
#include "driftwood/eval.hpp"
#include "driftwood/perft.hpp"
#include "driftwood/search.hpp"
#include "driftwood/serve.hpp"
#include "driftwood/tt.hpp"
#include "driftwood/uci.hpp"
#include "driftwood/movegen.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace driftwood;

static void print_usage() {
    std::cerr << "DriftWood Chess Engine" << std::endl;
    std::cerr << "Usage:" << std::endl;
    std::cerr << "  driftwood                      Run UCI protocol (default)" << std::endl;
    std::cerr << "  driftwood perft <depth> [fen] [--split]" << std::endl;
    std::cerr << "  driftwood fen [fen]" << std::endl;
    std::cerr << "  driftwood perftsuite" << std::endl;
    std::cerr << "  driftwood bench [depth]" << std::endl;
    std::cerr << "  driftwood selfplay <moves> [fen]" << std::endl;
    std::cerr << "  driftwood serve [port]        Start web UI (default port 8080)" << std::endl;
}

static int run_bench_command(int depth, int threads) {
    // A few benchmark positions
    static const char* bench_positions[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    };
    constexpr int NUM_POS = 6;

    TranspositionTable tt;
    tt.resize(64);
    Searcher searcher;
    searcher.set_tt(&tt);
    searcher.set_num_threads(threads);

    SearchLimits limits;
    limits.depth = depth;

    std::atomic<bool> stop_flag(false);
    searcher.set_stop_flag(&stop_flag);

    uint64_t total_nodes = 0;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_POS; ++i) {
        Board board = Board::from_fen(bench_positions[i]);
        searcher.set_limits(limits);
        if (threads > 1) {
            searcher.search_parallel(board, threads);
        } else {
            searcher.search(board);
        }
        total_nodes += searcher.last_nodes();
        std::cerr << "Position " << (i + 1) << "/" << NUM_POS
                  << " depth " << depth << " done" << std::endl;
    }

    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    uint64_t nps = total_nodes * 1000 / (ms > 0 ? ms : 1);

    std::cout << "Bench: depth " << depth
              << ", threads " << threads
              << ", positions " << NUM_POS
              << ", time " << ms << "ms"
              << ", nodes " << total_nodes
              << ", nps " << nps
              << std::endl;

    return 0;
}

static int run_selfplay_command(int moves, const std::string& fen) {
    Board board = fen.empty() ? Board::starting_position() : Board::from_fen(fen);

    TranspositionTable tt;
    tt.resize(64);
    Searcher searcher;
    searcher.set_tt(&tt);
    std::atomic<bool> stop_flag(false);
    searcher.set_stop_flag(&stop_flag);

    SearchLimits limits;
    limits.depth = 10; // fixed depth for self-play

    std::vector<std::string> move_list;
    move_list.reserve(static_cast<size_t>(moves));

    for (int i = 0; i < moves; ++i) {
        MoveList legal;
        generate_legal_moves(board, legal);
        if (legal.size() == 0) {
            std::cerr << "No legal moves at ply " << (i * 2) << std::endl;
            break;
        }

        searcher.set_limits(limits);
        SearchResult sr = searcher.search(board);
        if (sr.best_move.data == 0) {
            std::cerr << "Search returned no move at ply " << (i * 2) << std::endl;
            break;
        }

        move_list.push_back(sr.best_move.to_uci());
        board.make_move(sr.best_move);
    }

    // Print PGN-like output
    std::cout << "[Event \"Self-play\"]" << std::endl;
    std::cout << "[Site \"DriftWood\"]" << std::endl;
    std::cout << "[White \"DriftWood\"]" << std::endl;
    std::cout << "[Black \"DriftWood\"]" << std::endl;
    std::cout << std::endl;

    for (size_t i = 0; i < move_list.size(); i += 2) {
        int move_no = static_cast<int>(i / 2) + 1;
        std::cout << move_no << ". " << move_list[i];
        if (i + 1 < move_list.size()) {
            std::cout << " " << move_list[i + 1];
        }
        std::cout << " ";
        if ((i / 2 + 1) % 5 == 0) std::cout << std::endl;
    }
    std::cout << std::endl;

    return 0;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv, argv + argc);

    if (argc < 2) {
        // No args: run UCI mode
        UCIHandler uci;
        uci.loop();
        return 0;
    }

    const std::string& cmd = args[1];

    if (cmd == "perft") {
        if (argc < 3) {
            std::cerr << "Error: perft requires a depth argument" << std::endl;
            print_usage();
            return 1;
        }

        int depth = std::stoi(args[2]);

        bool split = false;
        for (int i = 3; i < argc; i++) {
            if (args[i] == "--split") {
                split = true;
                break;
            }
        }

        std::string fen;
        for (int i = 3; i < argc; i++) {
            if (args[i] == "--split") continue;
            if (!fen.empty()) fen += " ";
            fen += args[i];
        }
        if (fen.empty()) {
            fen = DEFAULT_FEN;
        }

        Board board = Board::from_fen(fen);

        if (split) {
            auto start = std::chrono::steady_clock::now();
            uint64_t total = perft_split(board, depth);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cerr << "\nTotal: " << total << std::endl;
            std::cerr << "Time: " << ms << "ms" << std::endl;
        } else {
            auto start = std::chrono::steady_clock::now();
            uint64_t result = perft(board, depth);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cout << result << std::endl;
            std::cerr << "Time: " << ms << "ms" << std::endl;
        }
    } else if (cmd == "fen") {
        if (argc < 3) {
            Board board = Board::starting_position();
            std::cout << board.to_fen() << std::endl;
        } else {
            std::string fen;
            for (int i = 2; i < argc; i++) {
                if (!fen.empty()) fen += " ";
                fen += args[i];
            }
            Board board = Board::from_fen(fen);
            std::cout << board.to_fen() << std::endl;
        }
    } else if (cmd == "perftsuite") {
        run_perftsuite();
    } else if (cmd == "bench") {
        int depth = 12;
        if (argc > 2) depth = std::stoi(args[2]);
        return run_bench_command(depth, 1);
    } else if (cmd == "genbook") {
        std::string path = "books/driftwood.bin";
        if (argc > 2) path = args[2];
        std::cerr << "Generating opening book to " << path << "..." << std::endl;
        if (generate_default_book(path)) {
            std::cerr << "Book generated successfully." << std::endl;
        } else {
            std::cerr << "Failed to generate book." << std::endl;
            return 1;
        }
    } else if (cmd == "selfplay") {
        if (argc < 3) {
            std::cerr << "Error: selfplay requires a move count" << std::endl;
            print_usage();
            return 1;
        }
        int moves = std::stoi(args[2]);
        std::string fen;
        for (int i = 3; i < argc; i++) {
            if (!fen.empty()) fen += " ";
            fen += args[i];
        }
        return run_selfplay_command(moves, fen);
    } else if (cmd == "serve") {
        int port = 8080;
        if (argc > 2) {
            port = std::stoi(args[2]);
            if (port < 1 || port > 65535) {
                std::cerr << "Error: invalid port number (use 1-65535)" << std::endl;
                return 1;
            }
        }
        return run_serve(port);
    } else {
        // Unknown command - check if it might be UCI commands (from pipe)
        // If not, try UCI mode
        UCIHandler uci;
        uci.loop();
    }

    return 0;
}
