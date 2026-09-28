#include "board.h"
#include "movegen.h"
#include "search.h"
#include "zobrist.h"
#include <atomic>
#include <iostream>
#include <sstream>

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

static Board board_from_fen(const std::string& fen) {
    Board board;
    board.squares.fill(EMPTY);

    std::istringstream ss(fen);
    std::string placement, active, castling, ep, halfmove, fullmove;
    ss >> placement >> active >> castling >> ep >> halfmove >> fullmove;

    int rank = 7;
    int file = 0;
    for (char c : placement) {
        if (c == '/') {
            rank--;
            file = 0;
        } else if (std::isdigit((unsigned char)c)) {
            file += c - '0';
        } else {
            board.squares[::sq(file++, rank)] = fen_piece(c);
        }
    }

    board.turn = (active == "w") ? WHITE : BLACK;
    board.castling_rights.K = castling.find('K') != std::string::npos;
    board.castling_rights.Q = castling.find('Q') != std::string::npos;
    board.castling_rights.k = castling.find('k') != std::string::npos;
    board.castling_rights.q = castling.find('q') != std::string::npos;
    board.en_passant_sq = (ep == "-") ? -1 : name_to_square(ep);
    board.halfmove_clock = halfmove.empty() ? 0 : std::stoi(halfmove);
    board.fullmove_number = fullmove.empty() ? 1 : std::stoi(fullmove);
    board.refresh_king_squares();
    return board;
}

int main() {
    init_zobrist();

    std::string fen =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Board board = board_from_fen(fen);

    std::atomic<bool> stop{false};

    Searcher searcher_new;
    Searcher searcher_orig;

    std::cout << "=== NEW vs ORIGINAL IMPLEMENTATION ===\n";

    for (int depth = 1; depth <= 6; depth++) {

        // =========================
        // NEW IMPLEMENTATION
        // =========================
        USE_NEW_LEGAL_MOVE_GEN = true;
        stop.store(false);
        searcher_new.nodes_searched = 0;

        Move new_move = searcher_new.find_best_move(
            board,
            depth,
            -1.0,
            -1,
            0,
            -1,
            {},
            1,
            nullptr,
            &stop
        );

        long long new_nodes = searcher_new.nodes_searched;


        // =========================
        // ORIGINAL IMPLEMENTATION
        // =========================
        USE_NEW_LEGAL_MOVE_GEN = false;
        stop.store(false);
        searcher_orig.nodes_searched = 0;

        Move orig_move = searcher_orig.find_best_move(
            board,
            depth,
            -1.0,
            -1,
            0,
            -1,
            {},
            1,
            nullptr,
            &stop
        );

        long long orig_nodes = searcher_orig.nodes_searched;


        // =========================
        // RESULTS
        // =========================
        std::cout << "\nDepth " << depth << "\n";

        std::cout << "  NEW:      "
                  << new_move.to_uci()
                  << " nodes=" << new_nodes << "\n";

        std::cout << "  ORIGINAL: "
                  << orig_move.to_uci()
                  << " nodes=" << orig_nodes << "\n";

        if (new_move == orig_move) {
            std::cout << "  RESULT: MATCH\n";
        } else {
            std::cout << "  RESULT: *** DIVERGENCE ***\n";
        }
    }

    return 0;
}
