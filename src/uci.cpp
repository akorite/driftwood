#include "driftwood/uci.hpp"
#include "driftwood/book.hpp"
#include "driftwood/eval.hpp"
#include "driftwood/movegen.hpp"
#include "driftwood/syzygy.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace driftwood {

// Version string
constexpr const char* ENGINE_VERSION = "0.4.0";

// Default book path
constexpr const char* DEFAULT_BOOK_PATH = "books/driftwood.bin";

UCIHandler::UCIHandler() {
    tt_.resize(64); // default 64 MB

    // Try to load the default opening book
    book_.load(DEFAULT_BOOK_PATH);
}

void UCIHandler::loop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        if (line == "uci") {
            cmd_uci();
        } else if (line == "isready") {
            cmd_isready();
        } else if (line == "ucinewgame") {
            cmd_ucinewgame();
        } else if (line.rfind("setoption", 0) == 0) {
            cmd_setoption(line);
        } else if (line.rfind("position", 0) == 0) {
            cmd_position(line);
        } else if (line.rfind("go", 0) == 0) {
            cmd_go(line);
        } else if (line == "stop") {
            cmd_stop();
        } else if (line == "print") {
            cmd_print();
        } else if (line == "quit") {
            break;
        }
        // Unknown commands are ignored
    }
}

void UCIHandler::cmd_uci() {
    std::cout << "id name DriftWood " << ENGINE_VERSION << std::endl;
    std::cout << "id author DriftWood" << std::endl;
    std::cout << "option name Hash type spin default 64 min 1 max 1024" << std::endl;
    std::cout << "option name Threads type spin default 1 min 1 max 256" << std::endl;
    std::cout << "option name BookFile type string default " << DEFAULT_BOOK_PATH << std::endl;
    std::cout << "option name BookMoves type spin default 12 min 0 max 100" << std::endl;
    std::cout << "option name SyzygyPath type string default <empty>" << std::endl;
    std::cout << "uciok" << std::endl;
}

void UCIHandler::cmd_isready() {
    std::cout << "readyok" << std::endl;
}

void UCIHandler::cmd_ucinewgame() {
    tt_.clear();
    board_ = Board::starting_position();
    searcher_.set_book(&book_);
    searcher_.set_num_threads(searcher_.num_threads()); // preserve thread count
}

void UCIHandler::cmd_setoption(const std::string& args) {
    // Parse: setoption name <NAME> value <VALUE>
    std::istringstream iss(args);
    std::string token;
    iss >> token; // "setoption"
    iss >> token; // "name"

    std::string name;
    while (iss >> token && token != "value") {
        if (!name.empty()) name += ' ';
        name += token;
    }

    std::string value;
    while (iss >> token) {
        if (!value.empty()) value += ' ';
        value += token;
    }

    if (name == "Hash") {
        int mb = std::stoi(value);
        mb = std::max(1, std::min(mb, 1024));
        tt_.resize(static_cast<size_t>(mb));
    } else if (name == "Threads") {
        int n = std::stoi(value);
        n = std::max(1, std::min(n, 256));
        searcher_.set_num_threads(n);
    } else if (name == "BookFile") {
        if (value.empty() || value == "<empty>") {
            // Clear the book
            book_ = OpeningBook();
        } else {
            book_.load(value);
        }
        searcher_.set_book(&book_);
    } else if (name == "BookMoves") {
        // BookMoves is not directly used by the engine; it's informational
        // The search will use the book as long as positions are found
        int moves = std::stoi(value);
        // Store for potential use
        (void)moves;
    } else if (name == "SyzygyPath") {
        if (value.empty() || value == "<empty>") {
            set_syzygy_path("");
        } else {
            init_syzygy(value);
        }
    }
}

void UCIHandler::cmd_position(const std::string& args) {
    // Parse: position [fen <FEN> | startpos] moves <m1> <m2> ...
    std::istringstream iss(args);
    std::string token;
    iss >> token; // "position"

    std::vector<std::string> tokens;
    while (iss >> token) {
        tokens.push_back(token);
    }

    if (tokens.empty()) {
        board_ = Board::starting_position();
        return;
    }

    size_t idx = 0;

    if (tokens[idx] == "startpos") {
        board_ = Board::starting_position();
        idx++;
    } else if (tokens[idx] == "fen") {
        idx++;
        std::string fen;
        int fen_parts = 0;
        while (idx < tokens.size() && fen_parts < 6 && tokens[idx] != "moves") {
            if (!fen.empty()) fen += ' ';
            fen += tokens[idx];
            fen_parts++;
            idx++;
        }
        if (fen_parts >= 4) {
            board_ = Board::from_fen(fen);
        } else {
            board_ = Board::starting_position();
        }
    } else {
        // Unknown; default to startpos
        board_ = Board::starting_position();
    }

    // Parse moves
    if (idx < tokens.size() && tokens[idx] == "moves") {
        idx++;
        while (idx < tokens.size()) {
            Move m = move_from_uci(board_, tokens[idx]);
            if (m.data != 0) {
                board_.make_move(m);
            }
            idx++;
        }
    }
}

void UCIHandler::cmd_go(const std::string& args) {
    // Parse: go depth N | movetime N | wtime N btime N [winc N binc N movestogo N] | infinite
    std::istringstream iss(args);
    std::string token;
    iss >> token; // "go"

    SearchLimits limits;

    while (iss >> token) {
        if (token == "depth") {
            int d;
            iss >> d;
            limits.depth = d;
        } else if (token == "nodes") {
            int64_t n;
            iss >> n;
            limits.nodes = n;
        } else if (token == "movetime") {
            int64_t t;
            iss >> t;
            limits.movetime = t;
        } else if (token == "wtime") {
            iss >> limits.wtime;
        } else if (token == "btime") {
            iss >> limits.btime;
        } else if (token == "winc") {
            iss >> limits.winc;
        } else if (token == "binc") {
            iss >> limits.binc;
        } else if (token == "movestogo") {
            iss >> limits.movestogo;
        } else if (token == "infinite") {
            limits.infinite = true;
        }
    }

    // Setup search
    searcher_.set_tt(&tt_);
    searcher_.set_limits(limits);
    searcher_.set_stop_flag(&stop_flag_);
    searcher_.set_book(&book_);

    stop_flag_.store(false);
    searching_ = true;

    // Before searching, try the opening book at the root
    uint16_t book_move_enc = searcher_.probe_book(board_);
    if (book_move_enc != 0) {
        // Decode the book move
        int from, to, promotion;
        book_decode_move(book_move_enc, from, to, promotion);

        // Verify the move is legal in the current position
        MoveList legal;
        generate_legal_moves(board_, legal);
        Move book_move;
        bool found = false;
        for (int i = 0; i < legal.size(); ++i) {
            Move m = legal[i];
            if (m.from().index == static_cast<uint8_t>(from) &&
                m.to().index == static_cast<uint8_t>(to)) {
                // Check promotion match
                if (promotion < 0 || (m.is_promotion() && static_cast<int>(m.promotion()) == promotion)) {
                    book_move = m;
                    found = true;
                    break;
                }
            }
        }

        if (found) {
            searching_ = false;
            std::cout << "bestmove " << book_move.to_uci() << std::endl;
            return;
        }
    }

    // Use parallel search if configured with multiple threads
    int num_threads = searcher_.num_threads();
    SearchResult result;
    if (num_threads > 1) {
        result = searcher_.search_parallel(board_, num_threads);
    } else {
        result = searcher_.search(board_);
    }

    searching_ = false;
    stop_flag_.store(false);

    // Output bestmove
    std::cout << "bestmove " << result.best_move.to_uci();
    if (result.ponder_move.data != 0) {
        std::cout << " ponder " << result.ponder_move.to_uci();
    }
    std::cout << std::endl;
}

void UCIHandler::cmd_stop() {
    stop_flag_.store(true);
}

void UCIHandler::cmd_print() {
    std::cout << board_.to_fen() << std::endl;
    MoveList moves;
    generate_legal_moves(board_, moves);
    std::cout << "Legal moves (" << moves.size() << "): ";
    for (int i = 0; i < moves.size(); ++i) {
        if (i > 0) std::cout << " ";
        std::cout << moves[i].to_uci();
    }
    std::cout << std::endl;
}

} // namespace driftwood
