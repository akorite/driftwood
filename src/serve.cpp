#include "driftwood/serve.hpp"
#include "driftwood/board.hpp"
#include "driftwood/book.hpp"
#include "driftwood/eval.hpp"
#include "driftwood/movegen.hpp"
#include "driftwood/search.hpp"
#include "driftwood/tt.hpp"
#include "driftwood/types.hpp"

#include "httplib.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace driftwood {
namespace {

// ---------------------------------------------------------------------------
// Helpers: JSON encoding (simple, no dependency)
// ---------------------------------------------------------------------------

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

static std::string J(const std::string& s) {
    return "\"" + json_escape(s) + "\"";
}

// Build a JSON object from a list of key:value pairs.
// Each pair is added via a helper that returns a string like "\"key\":value".
struct JsonPair {
    std::string data;
};
static JsonPair P(const std::string& k, const std::string& v) {
    return {J(k) + ":" + v};
}
static JsonPair PS(const std::string& k, const std::string& v) {
    return {J(k) + ":" + J(v)};
}
static JsonPair PI(const std::string& k, int64_t v) {
    return {J(k) + ":" + std::to_string(v)};
}
static JsonPair PB(const std::string& k, bool v) {
    return {J(k) + ":" + (v ? "true" : "false")};
}

static std::string json_object() {
    return "{}";
}

template <typename... Args>
static std::string json_object(const JsonPair& first, const Args&... rest) {
    std::string out = "{";
    out += first.data;
    // Use fold expression for the rest
    ((out += "," + rest.data), ...);
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// Helpers: simple JSON object parsing (minimal — just what we need)
// Looks for "key": "value" pairs in a JSON string.
// ---------------------------------------------------------------------------

static std::string json_extract_string(const std::string& body,
                                       const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = body.find(search);
    if (pos == std::string::npos) return "";

    pos += search.size();
    // Skip whitespace
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) ++pos;
    if (pos >= body.size()) return "";

    // Expect '"'
    if (body[pos] != '"') return "";
    ++pos;

    std::string value;
    while (pos < body.size()) {
        char c = body[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < body.size()) {
            char next = body[pos++];
            switch (next) {
                case '"':  value += '"';  break;
                case '\\': value += '\\'; break;
                case 'n':  value += '\n'; break;
                case 'r':  value += '\r'; break;
                case 't':  value += '\t'; break;
                case 'u': {
                    if (pos + 4 <= body.size()) {
                        value += "\\u" + body.substr(pos, 4);
                        pos += 4;
                    }
                    break;
                }
            }
        } else {
            value += c;
        }
    }
    return value;
}

// Extract an integer value for a given key from a JSON object body.
// Returns default_value if the key is missing or not a number.
static int64_t json_extract_int64(const std::string& body,
                                  const std::string& key,
                                  int64_t default_value) {
    std::string search = "\"" + key + "\":";
    auto pos = body.find(search);
    if (pos == std::string::npos) return default_value;
    pos += search.size();
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) ++pos;
    if (pos >= body.size()) return default_value;

    // Parse optional sign and digits
    bool negative = false;
    if (body[pos] == '-') { negative = true; ++pos; }
    else if (body[pos] == '+') { ++pos; }

    int64_t value = 0;
    bool any = false;
    while (pos < body.size() && body[pos] >= '0' && body[pos] <= '9') {
        value = value * 10 + (body[pos] - '0');
        ++pos;
        any = true;
    }
    if (!any) return default_value;
    return negative ? -value : value;
}

// ---------------------------------------------------------------------------
// Convert a Board position to result string
// ---------------------------------------------------------------------------

static std::string game_result_string(const Board& board) {
    if (board.is_checkmate()) {
        return (board.side_to_move() == Color::White) ? "0-1" : "1-0";
    }
    if (board.is_stalemate() ||
        board.is_insufficient_material() ||
        board.has_threefold_repetition() ||
        board.halfmove_clock() >= 100) {
        return "1/2-1/2";
    }
    return "*";
}

// ---------------------------------------------------------------------------
// Convert side to move to response char
// ---------------------------------------------------------------------------

static std::string side_to_char(Color c) {
    return (c == Color::White) ? "w" : "b";
}

// ---------------------------------------------------------------------------
// Shared engine state
// ---------------------------------------------------------------------------

struct EngineState {
    std::mutex mutex;
    TranspositionTable tt;
    Searcher searcher;
    OpeningBook book;
    std::atomic<bool> stop_flag{false};
    int64_t game_counter = 0;

    EngineState() {
        tt.resize(64); // 64 MB default
        searcher.set_tt(&tt);
        searcher.set_stop_flag(&stop_flag);
        // Try to load book (non-fatal if missing)
        if (!book.load("books/driftwood.bin")) {
            std::cerr << "[serve] No opening book found at books/driftwood.bin"
                      << std::endl;
        }
        searcher.set_book(&book);
    }
};

// Global engine state
static EngineState s_state;

// ---------------------------------------------------------------------------
// API: /api/new_game
// ---------------------------------------------------------------------------

static void handle_new_game(const httplib::Request& req,
                            httplib::Response& res) {
    auto color_str = req.get_param_value("color");
    Color human_color = Color::White;
    if (color_str == "black") {
        human_color = Color::Black;
    } else if (color_str == "random") {
        human_color = (std::rand() % 2 == 0) ? Color::White : Color::Black;
    }

    std::lock_guard<std::mutex> lock(s_state.mutex);
    s_state.game_counter++;

    Board board = Board::starting_position();

    // If human plays black, engine makes the first move as white
    std::string engine_move;
    if (human_color == Color::Black) {
        // Check opening book first
        uint16_t book_move = s_state.searcher.probe_book(board);
        if (book_move != 0) {
            int from, to, promotion;
            book_decode_move(book_move, from, to, promotion);
            MoveList legal;
            generate_legal_moves(board, legal);
            for (int i = 0; i < legal.size(); ++i) {
                Move m = legal[i];
                if (m.from().index == static_cast<uint8_t>(from) &&
                    m.to().index == static_cast<uint8_t>(to)) {
                    if (promotion < 0 ||
                        (m.is_promotion() &&
                         static_cast<int>(m.promotion()) == promotion)) {
                        engine_move = m.to_uci();
                        board.make_move(m);
                        break;
                    }
                }
            }
        }

        if (engine_move.empty()) {
            SearchLimits limits;
            limits.movetime = 2000;
            s_state.stop_flag.store(false);
            s_state.searcher.set_limits(limits);
            SearchResult sr = s_state.searcher.search(board);
            if (sr.best_move.data != 0) {
                engine_move = sr.best_move.to_uci();
                board.make_move(sr.best_move);
            }
        }
    }

    std::string body = json_object(
        PS("fen", board.to_fen()),
        PS("side_to_move", side_to_char(board.side_to_move())),
        PS("game_id", std::to_string(s_state.game_counter)),
        PS("human_color", (human_color == Color::White) ? "white" : "black"),
        PS("engine_move", engine_move)
    );

    res.set_content(body, "application/json");
}

// ---------------------------------------------------------------------------
// API: /api/move
// ---------------------------------------------------------------------------

static void handle_move(const httplib::Request& req,
                        httplib::Response& res) {
    // Parse JSON body
    std::string fen = json_extract_string(req.body, "fen");
    std::string move_uci = json_extract_string(req.body, "move");
    int64_t wtime_ms = json_extract_int64(req.body, "wtime", -1);
    int64_t btime_ms = json_extract_int64(req.body, "btime", -1);
    int64_t winc_ms  = json_extract_int64(req.body, "winc",  0);
    int64_t binc_ms  = json_extract_int64(req.body, "binc",  0);

    if (fen.empty()) {
        std::string body = json_object(
            PS("legal", "false"),
            PS("reason", "Missing fen field")
        );
        res.set_content(body, "application/json");
        return;
    }

    std::lock_guard<std::mutex> lock(s_state.mutex);

    Board board = Board::from_fen(fen);

    // If a move was provided by the user, validate and make it
    std::string user_move_uci;
    if (!move_uci.empty()) {
        Move user_move = move_from_uci(board, move_uci);
        if (user_move.data == 0) {
            std::string body = json_object(
                PS("legal", "false"),
                PS("reason", "Invalid UCI move format")
            );
            res.set_content(body, "application/json");
            return;
        }

        // Validate the move is legal
        MoveList legal;
        generate_legal_moves(board, legal);
        bool found = false;
        for (int i = 0; i < legal.size(); ++i) {
            if (legal[i].data == user_move.data) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::string body = json_object(
                PS("legal", "false"),
                PS("reason", "Illegal move")
            );
            res.set_content(body, "application/json");
            return;
        }

        // Make the user's move
        board.make_move(user_move);
        user_move_uci = move_uci;
    }

    // Check if the game is over after the user's move
    Color side_to_move = board.side_to_move();
    bool game_over = board.is_checkmate() || board.is_stalemate() ||
                     board.is_insufficient_material() ||
                     board.has_threefold_repetition() ||
                     board.halfmove_clock() >= 100;

    std::string engine_move_uci;
    if (!game_over) {
        // It's the engine's turn: search for a response.
        // Time management: pass wtime/btime from the client so the engine
        // spends proportionally less on low time. The search loop applies
        // a 3s hard cap and an aggressive divisor at low time. We leave
        // movetime unset so the wtime/btime budget takes effect.
        SearchLimits limits;
        if (wtime_ms > 0 || btime_ms > 0) {
            limits.wtime = wtime_ms;
            limits.btime = btime_ms;
            limits.winc  = winc_ms;
            limits.binc  = binc_ms;
        } else {
            limits.movetime = 2000; // legacy fallback when no clock given
        }
        s_state.stop_flag.store(false);
        s_state.searcher.set_limits(limits);

        // Check opening book first
        uint16_t book_move = s_state.searcher.probe_book(board);
        if (book_move != 0) {
            // Decode book move
            int from, to, promotion;
            book_decode_move(book_move, from, to, promotion);
            MoveList legal;
            generate_legal_moves(board, legal);
            for (int i = 0; i < legal.size(); ++i) {
                Move m = legal[i];
                if (m.from().index == static_cast<uint8_t>(from) &&
                    m.to().index == static_cast<uint8_t>(to)) {
                    if (promotion < 0 ||
                        (m.is_promotion() &&
                         static_cast<int>(m.promotion()) == promotion)) {
                        engine_move_uci = m.to_uci();
                        board.make_move(m);
                        break;
                    }
                }
            }
        }

        if (engine_move_uci.empty()) {
            SearchResult sr = s_state.searcher.search(board);
            if (sr.best_move.data != 0) {
                engine_move_uci = sr.best_move.to_uci();
                board.make_move(sr.best_move);
            }
        }
    }

    // Check game over after engine move
    bool final_game_over = board.is_checkmate() || board.is_stalemate() ||
                           board.is_insufficient_material() ||
                           board.has_threefold_repetition() ||
                           board.halfmove_clock() >= 100;

    std::string result = final_game_over ? game_result_string(board) : "*";

    std::string body = json_object(
        PS("legal", "true"),
        PS("fen", board.to_fen()),
        PS("side_to_move", side_to_char(board.side_to_move())),
        PS("user_move", user_move_uci),
        PS("engine_move", engine_move_uci),
        PS("is_check", board.is_check() ? "true" : "false"),
        PS("is_checkmate", board.is_checkmate() ? "true" : "false"),
        PS("is_stalemate", board.is_stalemate() ? "true" : "false"),
        PS("is_game_over", final_game_over ? "true" : "false"),
        PS("result", result)
    );

    res.set_content(body, "application/json");
}

// ---------------------------------------------------------------------------
// API: /api/state
// ---------------------------------------------------------------------------

static void handle_state(const httplib::Request& req,
                         httplib::Response& res) {
    auto fen = req.get_param_value("fen");
    if (fen.empty()) {
        std::string body = json_object(
            PS("error", "Missing fen parameter")
        );
        res.status = 400;
        res.set_content(body, "application/json");
        return;
    }

    Board board = Board::from_fen(fen);

    MoveList legal;
    generate_legal_moves(board, legal);

    // Build legal_moves JSON array
    std::string moves_json = "[";
    for (int i = 0; i < legal.size(); ++i) {
        if (i > 0) moves_json += ",";
        moves_json += J(legal[i].to_uci());
    }
    moves_json += "]";

    bool is_checkmate = board.is_checkmate();
    bool is_stalemate = board.is_stalemate();
    bool is_insuff = board.is_insufficient_material();
    bool is_rep = board.has_threefold_repetition();
    bool is_50 = board.halfmove_clock() >= 100;
    bool game_over = is_checkmate || is_stalemate || is_insuff || is_rep || is_50;

    std::string result = game_over ? game_result_string(board) : "*";

    std::string body = json_object(
        PS("fen", fen),
        P("legal_moves", moves_json),
        PS("is_check", board.is_check() ? "true" : "false"),
        PS("is_checkmate", is_checkmate ? "true" : "false"),
        PS("is_stalemate", is_stalemate ? "true" : "false"),
        PS("is_insufficient_material", is_insuff ? "true" : "false"),
        PS("is_threefold_repetition", is_rep ? "true" : "false"),
        PS("is_50moves", is_50 ? "true" : "false"),
        PS("is_game_over", game_over ? "true" : "false"),
        PS("result", result),
        PS("side_to_move", side_to_char(board.side_to_move()))
    );

    res.set_content(body, "application/json");
}

// ---------------------------------------------------------------------------
// API: /api/eval
// ---------------------------------------------------------------------------

static void handle_eval(const httplib::Request& req,
                        httplib::Response& res) {
    auto fen = req.get_param_value("fen");
    auto depth_str = req.get_param_value("depth");

    if (fen.empty()) {
        std::string body = json_object(
            PS("error", "Missing fen parameter")
        );
        res.status = 400;
        res.set_content(body, "application/json");
        return;
    }

    int depth = 10;
    if (!depth_str.empty()) {
        depth = std::stoi(depth_str);
        if (depth < 1) depth = 1;
        if (depth > 30) depth = 30;
    }

    std::lock_guard<std::mutex> lock(s_state.mutex);

    Board board = Board::from_fen(fen);

    SearchLimits limits;
    limits.depth = depth;
    s_state.stop_flag.store(false);
    s_state.searcher.set_limits(limits);

    auto start = std::chrono::steady_clock::now();
    SearchResult sr = s_state.searcher.search(board);
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    int64_t nodes = static_cast<int64_t>(s_state.searcher.last_nodes());
    int64_t nps_val = (ms > 0) ? (nodes * 1000 / ms) : 0;
    int score = s_state.searcher.last_score();

    // Parse PV from PV string
    std::string pv_str = s_state.searcher.last_pv_string();
    std::string pv_json = "[";
    if (!pv_str.empty()) {
        std::istringstream iss(pv_str);
        std::string token;
        bool first = true;
        while (iss >> token) {
            if (!first) pv_json += ",";
            pv_json += J(token);
            first = false;
        }
    }
    pv_json += "]";

    std::string body = json_object(
        PI("score_cp", score),
        PI("depth", depth),
        P("pv", pv_json),
        PI("nps", nps_val),
        PI("nodes", nodes),
        PI("time_ms", ms)
    );

    res.set_content(body, "application/json");
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

int run_serve(int port) {
    httplib::Server svr;

    // CORS headers for local development
    svr.set_pre_routing_handler([](const httplib::Request& req,
                                   httplib::Response& res) {
        if (req.method == "OPTIONS") {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods",
                           "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers",
                           "Content-Type");
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        res.set_header("Access-Control-Allow-Origin", "*");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // API endpoints (registered BEFORE static file handler)
    svr.Get("/api/new_game", handle_new_game);
    svr.Post("/api/move", handle_move);
    svr.Get("/api/state", handle_state);
    svr.Get("/api/eval", handle_eval);

    // Manual static file serving for web/ directory
    svr.Get(".*", [](const httplib::Request& req, httplib::Response& res) {
        // Only handle non-API paths
        if (req.path.compare(0, 5, "/api/") == 0) {
            res.status = 404;
            res.set_content(R"({"error":"Not found"})", "application/json");
            return;
        }

        // Determine file path
        std::string filepath = "./web";
        if (req.path == "/") {
            filepath += "/index.html";
        } else {
            filepath += req.path;
        }

        // Set content type based on extension
        std::string ext;
        auto dot = filepath.rfind('.');
        if (dot != std::string::npos) {
            ext = filepath.substr(dot);
        }

        std::string content_type = "text/plain";
        if (ext == ".html") content_type = "text/html; charset=utf-8";
        else if (ext == ".css") content_type = "text/css; charset=utf-8";
        else if (ext == ".js") content_type = "application/javascript; charset=utf-8";
        else if (ext == ".json") content_type = "application/json";
        else if (ext == ".svg") content_type = "image/svg+xml";
        else if (ext == ".png") content_type = "image/png";
        else if (ext == ".ico") content_type = "image/x-icon";

        res.set_file_content(filepath, content_type);
    });

    std::cout << "DriftWood Web UI" << std::endl;
    std::cout << "  Listening on http://localhost:" << port << std::endl;
    std::cout << "  Open your browser to http://localhost:" << port
              << std::endl;
    std::cout << "  Press Ctrl-C to stop." << std::endl;

    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "[serve] Failed to bind to port " << port << std::endl;
        return 1;
    }

    return 0;
}

} // namespace driftwood
