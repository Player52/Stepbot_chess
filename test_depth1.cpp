#include "board.h"
#include "movegen.h"
#include "search.h"
#include "zobrist.h"
#include <atomic>
#include <iostream>
#include <sstream>

// Declare the external variable
extern bool USE_NEW_LEGAL_MOVE_GEN;

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
    
    std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    Board board = board_from_fen(fen);
    
    USE_NEW_LEGAL_MOVE_GEN = true;
    SharedTT tt_new;
    std::atomic<bool> stop{false};
    Searcher searcher_new;
    
    std::ostringstream uci_sink;
    std::streambuf* old_cout = std::cout.rdbuf(uci_sink.rdbuf());
    
    for (int depth = 1; depth <= 5; depth++) {
        Move best = searcher_new.find_best_move(board, depth, -1.0, -1, 0, -1, {}, 1, &tt_new, &stop, 1, nullptr);
        long long nodes = searcher_new.nodes_searched;
        std::cout.rdbuf(old_cout);
        std::cout << "Depth " << depth << ": nodes=" << nodes << " best=" << best.to_uci() << std::endl;
        std::cout.rdbuf(uci_sink.rdbuf());
    }
    
    std::cout.rdbuf(old_cout);
    
    return 0;
}
