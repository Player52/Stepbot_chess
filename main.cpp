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

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

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

#include <fstream>
#include <unordered_map>

static std::unordered_map<std::string, std::vector<std::string>> opening_book;

static std::string fen_core(const std::string& full_fen) {
    std::istringstream ss(full_fen);
    std::string parts[4], tok;
    int i = 0;
    while (ss >> tok && i < 4) parts[i++] = tok;
    return parts[0] + " " + parts[1] + " " + parts[2] + " " + parts[3];
}

static void load_opening_book(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "  [Book] No opening book found at " << path << "\n";
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
    std::cerr << "  [Book] Loaded " << loaded << " entries\n";
}

static std::string book_lookup(const Board& board) {
    std::string core = fen_core(board_to_fen(board));
    auto it = opening_book.find(core);
    if (it == opening_book.end() || it->second.empty()) return "";
    return it->second[0];
}

struct UCIEngine {
    Board    board;
    Searcher searcher;
    int      default_depth = 9;
    int      max_depth     = 9;
    bool     use_book      = true;   // Can be disabled for data generation

    UCIEngine() : board(board_from_fen(STARTING_FEN)) {}

    void run() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            handle_command(line);
        }
    }

    void handle_command(const std::string& line) {
        auto tokens = split(line);
        if (tokens.empty()) return;
        const std::string& cmd = tokens[0];
        if      (cmd == "uci")        cmd_uci();
        else if (cmd == "isready")    cmd_isready();
        else if (cmd == "ucinewgame") cmd_ucinewgame();
        else if (cmd == "setoption")  cmd_setoption(tokens);
        else if (cmd == "position")   cmd_position(tokens);
        else if (cmd == "go")         cmd_go(tokens);
        else if (cmd == "stop")       { searcher.time_limit = 0; }
        else if (cmd == "quit")       std::exit(0);
        else if (cmd == "print")      board.print_board();
        else if (cmd == "fen")        { std::cout << board_to_fen(board) << "\n"; }
        else if (cmd == "moves") {
            auto moves = generate_legal_moves(board);
            std::cout << "Legal moves (" << moves.size() << "): ";
            for (const auto& m : moves) std::cout << m.to_uci() << " ";
            std::cout << "\n";
        }
    }

    void cmd_uci() {
        std::cout << "id name "   << ENGINE_NAME   << "\n";
        std::cout << "id author " << ENGINE_AUTHOR << "\n";
        std::cout << "option name MaxDepth type spin default 9 min 1 max 20\n";
        std::cout << "option name UseBook type check default true\n";
        std::cout << "uciok\n";
        std::cout.flush();
    }

    void cmd_setoption(const std::vector<std::string>& tokens) {
        // UCI setoption format: setoption name <name> value <value>
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
            std::cerr << "  [Option] MaxDepth set to " << max_depth << "\n";
        }
        if (opt_name == "UseBook" && !opt_value.empty()) {
            use_book = (opt_value == "true");
            std::cerr << "  [Option] UseBook set to "
                      << (use_book ? "true" : "false") << "\n";
        }
    }

    void cmd_isready() {
        std::cout << "readyok\n";
        std::cout.flush();
    }

    void cmd_ucinewgame() {
        board = board_from_fen(STARTING_FEN);
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
            for (; i < (int)tokens.size() && i < 8; i++) {
                if (tokens[i] == "moves") { move_start = i + 1; break; }
                if (!fen_str.empty()) fen_str += ' ';
                fen_str += tokens[i];
            }
            board = board_from_fen(fen_str);
        }
        if (move_start >= 0)
            for (int i = move_start; i < (int)tokens.size(); i++)
                board = apply_move(board, uci_to_move(tokens[i]));
    }

    void cmd_go(const std::vector<std::string>& tokens) {
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
        if (infinite) depth = 99;

        // Check opening book first (unless disabled)
        if (use_book) {
            std::string book_move = book_lookup(board);
            if (!book_move.empty()) {
                std::cout << "bestmove " << book_move << "\n";
                std::cout.flush();
                return;
            }
        }

        int    our_time_ms = (board.turn == WHITE) ? wtime : btime;
        int    our_inc_ms  = (board.turn == WHITE) ? winc  : binc;
        double tl_secs     = (movetime > 0) ? (movetime / 1000.0 - 0.05) : -1.0;
        int    budget_ms   = (movetime <= 0 && our_time_ms > 0 && depth >= 99)
                             ? our_time_ms : -1;

        Move best = searcher.find_best_move(board, depth,
                                            tl_secs, budget_ms, our_inc_ms, -1);

        if (best.from_sq == best.to_sq && best.from_sq == 0)
            std::cout << "bestmove 0000\n";
        else
            std::cout << "bestmove " << best.to_uci() << "\n";
        std::cout.flush();
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

int main() {
    init_zobrist();
    load_opening_book("opening_book.json");
    UCIEngine engine;
    engine.run();
    return 0;
}
