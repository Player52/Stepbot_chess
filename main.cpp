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
#include <sstream>    // std::istringstream — for splitting strings
#include <string>
#include <vector>
#include <algorithm>

// ─────────────────────────────────────────
// ENGINE INFO
// ─────────────────────────────────────────

const std::string ENGINE_NAME   = "Stepbot";
const std::string ENGINE_AUTHOR = "James";
const std::string STARTING_FEN  =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// ─────────────────────────────────────────
// FEN PARSER
// Converts a FEN string into a Board object.
// Same logic as board_from_fen() in engine.py.
// ─────────────────────────────────────────

// Map FEN characters to piece values
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

    // Split FEN into parts
    // std::istringstream lets us treat a string like a stream of words
    std::istringstream ss(fen);
    std::string piece_placement, active, castling, ep_str, halfmove, fullmove;
    ss >> piece_placement >> active >> castling >> ep_str >> halfmove >> fullmove;

    // Parse piece placement
    int rank = 7, file = 0;
    for (char c : piece_placement) {
        if (c == '/') {
            rank--;
            file = 0;
        } else if (std::isdigit(c)) {
            file += c - '0';
        } else {
            board.squares[sq(file, rank)] = fen_piece(c);
            file++;
        }
    }

    // Turn
    board.turn = (active == "w") ? WHITE : BLACK;

    // Castling rights
    board.castling_rights.K = (castling.find('K') != std::string::npos);
    board.castling_rights.Q = (castling.find('Q') != std::string::npos);
    board.castling_rights.k = (castling.find('k') != std::string::npos);
    board.castling_rights.q = (castling.find('q') != std::string::npos);

    // En passant
    board.en_passant_sq = (ep_str == "-") ? -1 : name_to_square(ep_str);

    // Clocks
    board.halfmove_clock  = halfmove.empty()  ? 0 : std::stoi(halfmove);
    board.fullmove_number = fullmove.empty()   ? 1 : std::stoi(fullmove);

    return board;
}

// ─────────────────────────────────────────
// FEN WRITER
// Converts a Board object back to a FEN string.
// ─────────────────────────────────────────

std::string board_to_fen(const Board& board) {
    std::string fen;

    for (int r = 7; r >= 0; r--) {
        int empty_count = 0;
        for (int f = 0; f < 8; f++) {
            int piece = board.get_piece(sq(f, r));
            if (piece == EMPTY) {
                empty_count++;
            } else {
                if (empty_count > 0) {
                    fen += ('0' + empty_count);
                    empty_count = 0;
                }
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

// ─────────────────────────────────────────
// SPLIT STRING HELPER
// Splits a string by spaces into a vector of tokens.
// Like Python's str.split()
// ─────────────────────────────────────────

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream ss(s);
    std::string token;
    // '>>' reads one whitespace-delimited token at a time
    while (ss >> token)
        tokens.push_back(token);
    return tokens;
}

// ─────────────────────────────────────────
// OPENING BOOK (lightweight inline version)
// The full book.py logic stays in Python for the self-play tools.
// Here we load opening_book.json at startup for use during play.
// ─────────────────────────────────────────

#include <fstream>            // For reading files
#include <unordered_map>

// Simple book: maps FEN (position core) -> list of UCI moves
static std::unordered_map<std::string, std::vector<std::string>> opening_book;

static std::string fen_core(const std::string& full_fen) {
    // Keep only the first 4 parts (strip clocks) — same as Python's _fen_core
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

    // Minimal JSON parser for our simple book format
    // We just scan for "fen": "...", "move": "..." patterns
    std::string line, fen, move;
    int loaded = 0;

    while (std::getline(f, line)) {
        // Find "fen": "..."
        auto fen_pos = line.find("\"fen\"");
        if (fen_pos != std::string::npos) {
            auto start = line.find('"', fen_pos + 5) + 1;
            auto end   = line.find('"', start);
            if (start != std::string::npos && end != std::string::npos)
                fen = line.substr(start, end - start);
        }

        // Find "move": "..."
        auto move_pos = line.find("\"move\"");
        if (move_pos != std::string::npos) {
            auto start = line.find('"', move_pos + 6) + 1;
            auto end   = line.find('"', start);
            if (start != std::string::npos && end != std::string::npos) {
                move = line.substr(start, end - start);
                if (!fen.empty() && !move.empty()) {
                    // Store by FEN core (no clocks)
                    opening_book[fen_core(fen)].push_back(move);
                    loaded++;
                    move.clear();
                    fen.clear();
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
    // Pick first move (weights handled by Python tools)
    return it->second[0];
}

// ─────────────────────────────────────────
// UCI ENGINE
// ─────────────────────────────────────────

struct UCIEngine {
    Board   board;
    Searcher searcher;
    int     default_depth = 4;

    UCIEngine() : board(board_from_fen(STARTING_FEN)) {}

    void run() {
        std::string line;

        // std::getline reads a full line from stdin
        // In Python this was input()
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
        else if (cmd == "position")   cmd_position(tokens);
        else if (cmd == "go")         cmd_go(tokens);
        else if (cmd == "stop")       {}   // No time management yet
        else if (cmd == "quit")       std::exit(0);
        else if (cmd == "print")      board.print_board();
        else if (cmd == "fen") {
            std::cout << board_to_fen(board) << "\n";
        }
        else if (cmd == "moves") {
            auto moves = generate_legal_moves(board);
            std::cout << "Legal moves (" << moves.size() << "): ";
            for (const auto& m : moves)
                std::cout << m.to_uci() << " ";
            std::cout << "\n";
        }
        // Unknown commands are silently ignored (UCI spec)
    }

    void cmd_uci() {
        std::cout << "id name "   << ENGINE_NAME   << "\n";
        std::cout << "id author " << ENGINE_AUTHOR << "\n";
        std::cout << "option name Depth type spin default 4 min 1 max 20\n";
        std::cout << "uciok\n";
        std::cout.flush();
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
            // Check for "moves" keyword
            for (int i = 2; i < (int)tokens.size(); i++) {
                if (tokens[i] == "moves") { move_start = i + 1; break; }
            }
        } else if (tokens[1] == "fen") {
            // FEN is tokens 2-7 (up to 6 parts)
            std::string fen_str;
            int i = 2;
            for (; i < (int)tokens.size() && i < 8; i++) {
                if (tokens[i] == "moves") { move_start = i + 1; break; }
                if (!fen_str.empty()) fen_str += ' ';
                fen_str += tokens[i];
            }
            board = board_from_fen(fen_str);
        }

        // Apply moves
        if (move_start >= 0) {
            for (int i = move_start; i < (int)tokens.size(); i++) {
                Move move = uci_to_move(tokens[i]);
                board = apply_move(board, move);
            }
        }
    }

    void cmd_go(const std::vector<std::string>& tokens) {
        int    depth      = default_depth;
        double time_limit = -1.0;

        for (int i = 1; i < (int)tokens.size(); i++) {
            if (tokens[i] == "depth" && i + 1 < (int)tokens.size())
                depth = std::stoi(tokens[i + 1]);
            else if (tokens[i] == "movetime" && i + 1 < (int)tokens.size())
                time_limit = std::stoi(tokens[i + 1]) / 1000.0;
            else if ((tokens[i] == "wtime" || tokens[i] == "btime")
                     && i + 1 < (int)tokens.size()) {
                bool our_time = (tokens[i] == "wtime" && board.turn == WHITE) ||
                                (tokens[i] == "btime" && board.turn == BLACK);
                if (our_time)
                    time_limit = std::stoi(tokens[i + 1]) / 1000.0 * 0.05;
            }
        }

        // Check opening book first
        std::string book_move = book_lookup(board);
        if (!book_move.empty()) {
            std::cout << "bestmove " << book_move << "\n";
            std::cout.flush();
            return;
        }

        // Search
        Move best = searcher.find_best_move(board, depth, time_limit);

        if (best.from_sq == best.to_sq && best.from_sq == 0) {
            std::cout << "bestmove 0000\n";
        } else {
            std::cout << "bestmove " << best.to_uci() << "\n";
        }
        std::cout.flush();
    }

    // Convert a UCI string to a Move
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

// ─────────────────────────────────────────
// MAIN
// Program entry point — like Python's if __name__ == "__main__"
// ─────────────────────────────────────────

int main() {
    // Initialise Zobrist tables before anything else
    init_zobrist();

    // Load opening book
    load_opening_book("opening_book.json");

    // Run the UCI engine
    UCIEngine engine;
    engine.run();

    return 0;
}
