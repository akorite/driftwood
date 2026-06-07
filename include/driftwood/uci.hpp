#pragma once

#include "driftwood/board.hpp"
#include "driftwood/search.hpp"
#include "driftwood/tt.hpp"
#include "driftwood/book.hpp"

#include <atomic>
#include <string>

namespace driftwood {

// UCI protocol handler.
// Reads from stdin line-by-line and dispatches commands.
class UCIHandler {
public:
    UCIHandler();
    void loop();

    // Accessors for tests
    OpeningBook& book() { return book_; }

private:
    void cmd_uci();
    void cmd_isready();
    void cmd_ucinewgame();
    void cmd_setoption(const std::string& args);
    void cmd_position(const std::string& args);
    void cmd_go(const std::string& args);
    void cmd_stop();
    void cmd_print();

    // Search
    Searcher searcher_;
    Board board_;
    TranspositionTable tt_;
    OpeningBook book_;
    std::atomic<bool> stop_flag_{false};
    bool searching_ = false;
};

} // namespace driftwood
