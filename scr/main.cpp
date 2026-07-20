// main.cpp
// UCI protocol interface and engine entry point.
// C++ equivalent of engine.py + run.py combined.
//
// Compile with the Makefile:
//   make
//
// Or manually:
//   g++ -O2 -std=c++17 -o stepbot main.cpp board.cpp movegen.cpp evaluate.cpp zobrist.cpp search.cpp
//
// Run:
//   ./stepbot         (Linux/Mac)
//   stepbot.exe       (Windows)

#include "board.h"
#include "movegen.h"
#include "evaluate.h"
#include "search.h"
#include "zobrist.h"
#include "stepbot_live_writer.h"
#include <chrono>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>

const std::string ENGINE_NAME   = "Stepbot";
const std::string ENGINE_AUTHOR = "James";
const std::string STARTING_FEN  =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static int fen_piece(char c) {
    switch (c) {
        case 'P': return  PAWN;   case 'p': return -PAWN;
        case 'N': return  KNIGHT; case 'n': return -KNIGHT;
        case 'B': return  BISHOP; case 'b': return -BISHOP;
        case 'R': return  ROOK;   case 'r': return -ROOK;
        case 'Q': return  QUEEN;  case 'q': return -QUEEN;
        case 'K': return  KING;   case 'k': return -KING;
        default:  return  EMPTY;
    }
}

Board board_from_fen(const std::string& fen) {
    Board board;
    board.squares.fill(EMPTY);
    std::istringstream ss(fen);
    std::string piece_placement, active, castling, ep_str, halfmove, fullmove;
    ss >> piece_placement >> active >> castling >> ep_str >> halfmove >> fullmove;
    int rank = 7, file = 0;
    for (char c : piece_placement) {
        if (c == '/') { rank--; file = 0; }
        else if (std::isdigit(c)) { file += c - '0'; }
        else { board.squares[sq(file, rank)] = fen_piece(c); file++; }
    }
    board.turn = (active == "w") ? WHITE : BLACK;
    board.castling_rights.K = (castling.find('K') != std::string::npos);
    board.castling_rights.Q = (castling.find('Q') != std::string::npos);
    board.castling_rights.k = (castling.find('k') != std::string::npos);
    board.castling_rights.q = (castling.find('q') != std::string::npos);
    board.en_passant_sq = (ep_str == "-") ? -1 : name_to_square(ep_str);
    board.halfmove_clock  = halfmove.empty()  ? 0 : std::stoi(halfmove);
    board.fullmove_number = fullmove.empty()   ? 1 : std::stoi(fullmove);
    board.refresh_king_squares();
    return board;
}

std::string board_to_fen(const Board& board) {
    std::string fen;
    for (int r = 7; r >= 0; r--) {
        int empty_count = 0;
        for (int f = 0; f < 8; f++) {
            int piece = board.get_piece(sq(f, r));
            if (piece == EMPTY) { empty_count++; }
            else {
                if (empty_count > 0) { fen += ('0' + empty_count); empty_count = 0; }
                fen += piece_symbol(piece);
            }
        }
        if (empty_count > 0) fen += ('0' + empty_count);
        if (r > 0) fen += '/';
    }
    fen += ' ';
    fen += (board.turn == WHITE) ? 'w' : 'b';
    fen += ' ';
    std::string castling;
    if (board.castling_rights.K) castling += 'K';
    if (board.castling_rights.Q) castling += 'Q';
    if (board.castling_rights.k) castling += 'k';
    if (board.castling_rights.q) castling += 'q';
    fen += castling.empty() ? "-" : castling;
    fen += ' ';
    fen += (board.en_passant_sq == -1) ? "-" : square_name(board.en_passant_sq);
    fen += ' ';
    fen += std::to_string(board.halfmove_clock);
    fen += ' ';
    fen += std::to_string(board.fullmove_number);
    return fen;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream ss(s);
    std::string token;
    while (ss >> token) tokens.push_back(token);
    return tokens;
}

static std::unordered_map<std::string, std::vector<std::string>> opening_book;

static std::string fen_core(const std::string& full_fen) {
    std::istringstream ss(full_fen);
    std::string parts[4], tok;
    int i = 0;
    while (ss >> tok && i < 4) parts[i++] = tok;
    return parts[0] + " " + parts[1] + " " + parts[2] + " " + parts[3];
}

static bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

static std::string dirname_of(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return "";
    return path.substr(0, pos);
}

static std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir.back();
    if (last == '/' || last == '\\') return dir + name;
    return dir + "/" + name;
}

static std::string resolve_opening_book_path(const char* argv0) {
    std::vector<std::string> candidates = {
        "opening_book.json",
        "../opening_book.json"
    };

    std::string exe_dir = argv0 ? dirname_of(argv0) : "";
    if (!exe_dir.empty()) {
        candidates.push_back(join_path(exe_dir, "opening_book.json"));
        candidates.push_back(join_path(exe_dir, "../opening_book.json"));
    }

    for (const auto& path : candidates) {
        if (file_exists(path)) return path;
    }
    return candidates.front();
}

static void load_opening_book(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        return;
    }
    std::string line, fen, move;
    int loaded = 0;
    while (std::getline(f, line)) {
        auto fen_pos = line.find("\"fen\"");
        if (fen_pos != std::string::npos) {
            auto start = line.find('"', fen_pos + 5) + 1;
            auto end   = line.find('"', start);
            if (start != std::string::npos && end != std::string::npos)
                fen = line.substr(start, end - start);
        }
        auto move_pos = line.find("\"move\"");
        if (move_pos != std::string::npos) {
            auto start = line.find('"', move_pos + 6) + 1;
            auto end   = line.find('"', start);
            if (start != std::string::npos && end != std::string::npos) {
                move = line.substr(start, end - start);
                if (!fen.empty() && !move.empty()) {
                    opening_book[fen_core(fen)].push_back(move);
                    loaded++;
                    move.clear(); fen.clear();
                }
            }
        }
    }
    (void)loaded;
}

static std::string book_lookup(const Board& board) {
    std::string core = fen_core(board_to_fen(board));
    auto it = opening_book.find(core);
    if (it == opening_book.end() || it->second.empty()) return "";
    return it->second[0];
}

static bool is_null_move(const Move& move) {
    return move.from_sq == 0 && move.to_sq == 0 && move.promotion == 0;
}

struct UCIEngine {
    Board              board;
    Searcher           searcher;   // main thread searcher
    SMPPool            smp;        // shared TT + helper threads
    std::vector<Hash>  position_history;
    int                max_depth   = 9;
    int                multipv     = 1;
    bool               use_book    = true;
    std::thread        search_thread;
    std::mutex         search_mutex;
    std::mutex         output_mutex;
    std::atomic<bool>  search_running{false};
    std::atomic<bool>  suppress_bestmove{false};

    UCIEngine() : board(board_from_fen(STARTING_FEN)) {
        // Wire main searcher to shared TT
        searcher.tt        = &smp.shared_tt;
        searcher.thread_id = 0;
        searcher.stop_flag = &smp.stop;
        smp.resize(1);  // single-threaded by default
    }

    ~UCIEngine() {
        stop_search(true, false);
    }

    void write_output(const std::string& text) {
        std::lock_guard<std::mutex> lock(output_mutex);
        std::cout << text;
        std::cout.flush();
    }

    void stop_search(bool request_stop, bool emit_bestmove = true) {
        std::lock_guard<std::mutex> lock(search_mutex);
        if (search_thread.joinable()) {
            suppress_bestmove.store(!emit_bestmove, std::memory_order_relaxed);
            if (request_stop)
                smp.stop.store(true, std::memory_order_relaxed);
            search_thread.join();
        }
        search_running.store(false, std::memory_order_relaxed);
    }

    void run() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            handle_command(line);
        }
        stop_search(true, false);
    }

    void handle_command(const std::string& line) {
        auto tokens = split(line);
        if (tokens.empty()) return;
        const std::string& cmd = tokens[0];
        if      (cmd == "uci")        cmd_uci();
        else if (cmd == "isready")    cmd_isready();
        else if (cmd == "ucinewgame") { stop_search(true, false); cmd_ucinewgame(); }
        else if (cmd == "setoption")  { stop_search(true, false); cmd_setoption(tokens); }
        else if (cmd == "position")   { stop_search(true, false); cmd_position(tokens); }
        else if (cmd == "go")         cmd_go(tokens);
        else if (cmd == "stop")       stop_search(true);
        else if (cmd == "quit")       { stop_search(true, false); std::exit(0); }
        else if (cmd == "print")      board.print_board();
        else if (cmd == "fen")        { write_output(board_to_fen(board) + "\n"); }
        else if (cmd == "moves") {
            auto moves = generate_legal_moves(board);
            std::ostringstream out;
            out << "Legal moves (" << moves.size() << "): ";
            for (const auto& m : moves) out << m.to_uci() << " ";
            out << "\n";
            write_output(out.str());
        }
    }

    void cmd_uci() {
        std::ostringstream out;
        out << "id name "   << ENGINE_NAME   << "\n";
        out << "id author " << ENGINE_AUTHOR << "\n";
        out << "option name MaxDepth type spin default 9 min 1 max 20\n";
        out << "option name UseBook type check default true\n";
        out << "option name Threads type spin default 1 min 1 max 16\n";
        out << "option name Hash type spin default "
            << TT_DEFAULT_HASH_MB << " min " << TT_MIN_HASH_MB
            << " max " << TT_MAX_HASH_MB << "\n";
        out << "option name MultiPV type spin default 1 min 1 max 5\n";
        out << "uciok\n";
        write_output(out.str());
    }

    void cmd_setoption(const std::vector<std::string>& tokens) {
        // UCI setoption format: setoption name <n> value <value>
        // We scan for "name" and "value" keywords in the token list
        std::string opt_name, opt_value;
        for (int i = 1; i < (int)tokens.size(); i++) {
            if (tokens[i] == "name"  && i + 1 < (int)tokens.size())
                opt_name  = tokens[++i];
            if (tokens[i] == "value" && i + 1 < (int)tokens.size())
                opt_value = tokens[++i];
        }

        if (opt_name == "MaxDepth" && !opt_value.empty()) {
            int val   = std::stoi(opt_value);
            val       = std::max(1, std::min(20, val));
            max_depth = val;
        }
        if (opt_name == "UseBook" && !opt_value.empty()) {
            use_book = (opt_value == "true");
        }
        if (opt_name == "Threads" && !opt_value.empty()) {
            int val = std::stoi(opt_value);
            val = std::max(1, std::min(16, val));
            smp.resize(val);
            // Re-wire main searcher in case helpers vector was reallocated
            searcher.tt        = &smp.shared_tt;
            searcher.stop_flag = &smp.stop;
        }
        if (opt_name == "Hash" && !opt_value.empty()) {
            int val = std::stoi(opt_value);
            smp.stop_helpers();
            smp.shared_tt.resize_mb(val);
            searcher.tt = &smp.shared_tt;
        }
        if (opt_name == "MultiPV" && !opt_value.empty()) {
            int val = std::stoi(opt_value);
            multipv = std::max(1, std::min(5, val));
        }
    }

    void cmd_isready() {
        write_output("readyok\n");
    }

    void cmd_ucinewgame() {
        board = board_from_fen(STARTING_FEN);
        position_history.clear();
        smp.stop_helpers();
        smp.new_game();
        searcher.tt_new_game();
    }

    bool legal_move_from_uci(const Board& source, const std::string& uci, Move& out) {
        auto legal = generate_legal_moves(source);
        for (const Move& move : legal) {
            if (move.to_uci() == uci) {
                out = move;
                return true;
            }
        }
        return false;
    }

    Move legal_bestmove_or_fallback(const Board& source, const Move& requested) {
        auto legal = generate_legal_moves(source);
        if (legal.empty()) return Move(0, 0);

        for (const Move& move : legal) {
            if (move == requested) return requested;
        }

        return legal.front();
    }

    void write_bestmove(const Board& source, const Move& requested) {
        Move safe = legal_bestmove_or_fallback(source, requested);
        if (is_null_move(safe))
            write_output("bestmove 0000\n");
        else
            write_output("bestmove " + safe.to_uci() + "\n");
    }

    void cmd_position(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return;
        int move_start = -1;
        if (tokens[1] == "startpos") {
            board = board_from_fen(STARTING_FEN);
            for (int i = 2; i < (int)tokens.size(); i++)
                if (tokens[i] == "moves") { move_start = i + 1; break; }
        } else if (tokens[1] == "fen") {
            std::string fen_str;
            int i = 2;
            for (; i < (int)tokens.size(); i++) {
                if (tokens[i] == "moves") { move_start = i + 1; break; }
                if (!fen_str.empty()) fen_str += ' ';
                fen_str += tokens[i];
            }
            board = board_from_fen(fen_str);
        }
        // Rebuild position history from scratch each time position is set
        position_history.clear();
        position_history.push_back(compute_hash(board));
        if (move_start >= 0) {
            for (int i = move_start; i < (int)tokens.size(); i++) {
                Move move;
                if (!legal_move_from_uci(board, tokens[i], move)) {
                    break;
                }
                board = apply_move(board, move);
                position_history.push_back(compute_hash(board));
            }
        }
    }

    void cmd_go(const std::vector<std::string>& tokens) {
        stop_search(true, false);

        // Start with max_depth as ceiling — can be lowered by "go depth N"
        // but never raised above max_depth
        int    depth    = max_depth;
        int    wtime    = -1, btime = -1, winc = 0, binc = 0, movetime = -1;
        bool   infinite = false;

        for (int i = 1; i < (int)tokens.size(); i++) {
            auto get_next_int = [&]() {
                return (i + 1 < (int)tokens.size()) ? std::stoi(tokens[++i]) : 0;
            };
            if      (tokens[i] == "depth")    { depth    = std::min(get_next_int(), max_depth); }
            else if (tokens[i] == "movetime") { movetime = get_next_int(); }
            else if (tokens[i] == "wtime")    { wtime    = get_next_int(); }
            else if (tokens[i] == "btime")    { btime    = get_next_int(); }
            else if (tokens[i] == "winc")     { winc     = get_next_int(); }
            else if (tokens[i] == "binc")     { binc     = get_next_int(); }
            else if (tokens[i] == "infinite") { infinite = true; }
        }

        // "infinite" ignores max_depth — used for analysis mode
        if (infinite) depth = MAX_DEPTH - 1;

        // movetime mode: uncap depth so the engine uses the full time budget
        if (movetime > 0) depth = MAX_DEPTH - 1;

        // clock mode (wtime/btime): also uncap depth so time management drives search
        if (wtime > 0 || btime > 0) depth = MAX_DEPTH - 1;

        // Check opening book first (unless disabled)
        if (use_book && !infinite) {
            std::string book_move = book_lookup(board);
            if (!book_move.empty()) {
                Move move;
                if (legal_move_from_uci(board, book_move, move)) {
                    write_bestmove(board, move);
                    return;
                }
            }
        }

        int    our_time_ms = (board.turn == WHITE) ? wtime : btime;
        int    our_inc_ms  = (board.turn == WHITE) ? winc  : binc;
        double tl_secs     = (movetime > 0) ? (movetime / 1000.0 - 0.05) : -1.0;
        int    budget_ms   = (movetime <= 0 && our_time_ms > 0 && depth >= MAX_DEPTH - 1)
                             ? our_time_ms : -1;

        // Launch helper threads (they share the TT and stop flag)
        Board board_snapshot = board;
        std::vector<Hash> history_snapshot = position_history;
        int multipv_snapshot = multipv;
        int thread_count_snapshot = smp.num_threads;

        smp.stop.store(false, std::memory_order_relaxed);
        suppress_bestmove.store(false, std::memory_order_relaxed);
        smp.threads.clear();
        search_running.store(true, std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(search_mutex);
        search_thread = std::thread([this, board_snapshot, history_snapshot,
                                     depth, tl_secs, budget_ms, our_inc_ms,
                                     thread_count_snapshot,
                                     multipv_snapshot]() mutable {
            LiveWriter::Guard liveGuard;

            Hash root_hash = compute_hash(board_snapshot);
            double search_start = []() {
                using namespace std::chrono;
                auto t = steady_clock::now().time_since_epoch();
                return duration_cast<duration<double>>(t).count();
            }();

            for (int i = 0; i < (int)smp.helpers.size(); i++) {
                smp.helpers[i].position_history = history_snapshot;
                smp.helpers[i].start_time  = search_start;
                smp.helpers[i].time_limit  = -1;
                smp.helpers[i].soft_limit  = -1;
                Board helper_board = board_snapshot;
                Searcher* helper_ptr = &smp.helpers[i];
                smp.threads.emplace_back([helper_ptr, helper_board, root_hash, depth]() mutable {
                    helper_ptr->helper_search(helper_board, root_hash, depth);
                });
            }

            Move best = searcher.find_best_move(board_snapshot, depth,
                                                tl_secs, budget_ms, our_inc_ms, -1,
                                                history_snapshot,
                                                thread_count_snapshot,
                                                &smp.shared_tt,
                                                &smp.stop,
                                                multipv_snapshot,
                                                &output_mutex);

            smp.stop_helpers();

            if (!suppress_bestmove.load(std::memory_order_relaxed)) {
                write_bestmove(board_snapshot, best);
            }

            search_running.store(false, std::memory_order_relaxed);
        });
    }

    Move uci_to_move(const std::string& uci) {
        int from  = name_to_square(uci.substr(0, 2));
        int to    = name_to_square(uci.substr(2, 2));
        int promo = 0;
        if (uci.size() == 5) {
            switch (uci[4]) {
                case 'n': promo = KNIGHT; break;
                case 'b': promo = BISHOP; break;
                case 'r': promo = ROOK;   break;
                case 'q': promo = QUEEN;  break;
            }
        }
        return Move(from, to, promo);
    }
};

int main(int argc, char* argv[]) {
    init_zobrist();
    load_opening_book(resolve_opening_book_path(argc > 0 ? argv[0] : nullptr));
    UCIEngine engine;
    engine.run();
    return 0;
}
