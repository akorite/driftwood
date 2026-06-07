#include "driftwood/syzygy.hpp"
#include "driftwood/board.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace driftwood {

// ---------------------------------------------------------------------------
// Syzygy tablebase state
//
// Phase 3 implementation: provides the probe API and integration points.
// When a Syzygy path is configured and tablebase files for the given
// piece count are found, the module probes them. Otherwise, it returns
// false and the engine falls back to search + evaluation.
//
// For a full implementation, include the Fathom (Ronald de Man) library
// or re-implement the Syzygy decompressor. The stub below checks for the
// presence of WDL files as a proxy for availability.
// ---------------------------------------------------------------------------

static std::string syzygy_path_;
static bool syzygy_initialized_ = false;
static int max_pieces_ = 0; // 0 = not loaded

// Expected WDL file names for 3-6 pieces (excluding kings)
// Format: rtbw (WDL) files
static const char* wdl_files_3[] = {"KvK.rtbw"}; // 2 pieces (K vs K) — handled by insufficient material
static const char* wdl_files_4[] = {"KNNvK.rtbw", "KBNvK.rtbw", "KBBvK.rtbw", "KRvK.rtbw",
                                    "KQvK.rtbw", "KvKR.rtbw", "KvKQ.rtbw", "KvKBB.rtbw",
                                    "KvKBN.rtbw", "KvKNN.rtbw"};

void set_syzygy_path(const std::string& path) {
    syzygy_path_ = path;
    syzygy_initialized_ = false;
    max_pieces_ = 0;
}

const std::string& get_syzygy_path() {
    return syzygy_path_;
}

bool init_syzygy(const std::string& path) {
    syzygy_path_ = path;
    syzygy_initialized_ = false;
    max_pieces_ = 0;

    if (path.empty()) {
        return false;
    }

    // Check if the path exists and has at least one .rtbw file
    // This is a simple check — a real implementation would load the
    // tablebase headers.
    std::string wdl_test = path + "/KvK.rtbw";
    std::ifstream test(wdl_test, std::ios::binary);
    if (test.good()) {
        max_pieces_ = 2; // minimal
        syzygy_initialized_ = true;
        std::cerr << "info string Syzygy: found at least K v K tablebase" << std::endl;
        return true;
    }

    // Try checking for any .rtbw file
    // Real implementation: iterate directory, parse file names for piece counts
    // For the stub, we'll just say we found basic tablebases if the path is non-empty
    // and the directory exists
    std::ifstream dir_test(path + "/.");
    if (dir_test.good()) {
        // Directory exists but no specific files — report minimal availability
        max_pieces_ = 2;
        syzygy_initialized_ = true;
        std::cerr << "info string Syzygy: path set but no .rtbw files found (will fall back to eval)" << std::endl;
        return true;
    }

    std::cerr << "info string Syzygy: path '" << path << "' not accessible" << std::endl;
    return false;
}

int syzygy_max_pieces() {
    return max_pieces_;
}

bool syzygy_available(const Board& board) {
    if (!syzygy_initialized_ || syzygy_path_.empty()) return false;

    int total = piece_count(board);
    // Count non-king pieces
    int non_kings = total - 2;
    if (non_kings <= 0) return true; // K vs K — always draw
    if (non_kings <= max_pieces_) return true;
    return false;
}

int piece_count(const Board& board) {
    return __builtin_popcountll(board.all_pieces());
}

bool probe_wdl(const Board& board, int& wdl) {
    if (!syzygy_available(board)) return false;

    int total = piece_count(board);

    // Stub: handle trivial cases
    if (total <= 2) {
        // K vs K — draw
        wdl = 0;
        return true;
    }

    // For a real implementation, this would:
    // 1. Normalize the position (swap colors if necessary)
    // 2. Compute the TB index from the pieces
    // 3. Look up the decompressed WDL entry
    // 4. Return the WDL value

    // For now, return false to fall back to eval
    return false;
}

bool probe_dtz(const Board& board, int& dtz) {
    if (!syzygy_available(board)) return false;

    int total = piece_count(board);

    // Stub: handle trivial cases
    if (total <= 2) {
        dtz = 0;
        return true;
    }

    // For a real implementation, this would probe DTZ similarly.
    // Fall back for now.
    return false;
}

} // namespace driftwood
