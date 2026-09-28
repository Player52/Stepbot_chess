#include "board.h"
#include "movegen.h"
#include "search.h"
#include "zobrist.h"
#include <atomic>
#include <iostream>
#include <sstream>
#include <vector>

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

// Patch search.h to add tracing
// We'll create a wrapper that logs key events

class TracingSearcher : public Searcher {
public:
    std::vector<std::string> trace_log;
    int trace_depth = 0;
    
    // Override find_best_move to trace the root
    Move find_best_move_with_trace(Board& board, int depth, double soft_limit, double hard_limit,
                                     int seldepth_limit, int given_alpha, int given_beta,
                                     const std::vector<Move>& search_moves, int multipv,
                                     SharedTT* shared_tt, std::atomic<bool>* stop_requested,
                                     int thread_id, NNUEState* nnue_state) {
        trace_log.clear();
        trace_depth = 0;
        log("ROOT: depth=" + std::to_string(depth) + ", alpha=" + std::to_string(given_alpha) + ", beta=" + std::to_string(given_beta));
        
        // Call the base search
        Move result = find_best_move(board, depth, soft_limit, hard_limit, seldepth_limit, given_alpha, given_beta,
                                       search_moves, multipv, shared_tt, stop_requested, thread_id, nnue_state);
        
        log("ROOT done: nodes=" + std::to_string(nodes_searched) + ", best=" + result.to_uci());
        
        return result;
    }
    
    // We can't easily override search_root without modifying search.cpp
    // So let's just manually trace the first level
    
    void log(const std::string& msg) {
        std::string indent(trace_depth * 2, ' ');
        trace_log.push_back(indent + msg);
        std::cout << indent << msg << std::endl;
    }
};

int main() {
    init_zobrist();
    
    std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    Board board = board_from_fen(fen);
    
    // Test with new implementation at depth 2
    std::cout << "=== NEW IMPLEMENTATION (USE_NEW_LEGAL_MOVE_GEN=true) ===" << std::endl;
    USE_NEW_LEGAL_MOVE_GEN = true;
    
    SharedTT tt;
    std::atomic<bool> stop{false};
    TracingSearcher searcher;
    
    // First, check legal moves at root
    MoveList moves;
    generate_legal_moves_into_ab(board, moves);
    std::cout << "Root legal moves: " << moves.size() << std::endl;
    
    // Now run search at depth 1
    std::cout << "\n=== DEPTH 1 ===" << std::endl;
    Move best_d1 = searcher.find_best_move(board, 1, -1.0, -1, 0, -1, {}, 1, &tt, &stop, 1, nullptr);
    std::cout << "Depth 1: nodes=" << searcher.nodes_searched << " best=" << best_d1.to_uci() << std::endl;
    
    // Clear TT and run depth 2
    tt.clear();
    searcher.nodes_searched = 0;
    
    std::cout << "\n=== DEPTH 2 ===" << std::endl;
    Move best_d2 = searcher.find_best_move(board, 2, -1.0, -1, 0, -1, {}, 1, &tt, &stop, 1, nullptr);
    std::cout << "Depth 2: nodes=" << searcher.nodes_searched << " best=" << best_d2.to_uci() << std::endl;
    
    // Now test with original implementation
    std::cout << "\n\n=== ORIGINAL IMPLEMENTATION (USE_NEW_LEGAL_MOVE_GEN=false) ===" << std::endl;
    USE_NEW_LEGAL_MOVE_GEN = false;
    
    SharedTT tt_orig;
    TracingSearcher searcher_orig;
    
    std::cout << "Depth 1:" << std::endl;
    best_d1 = searcher_orig.find_best_move(board, 1, -1.0, -1, 0, -1, {}, 1, &tt_orig, &stop, 1, nullptr);
    std::cout << "  nodes=" << searcher_orig.nodes_searched << " best=" << best_d1.to_uci() << std::endl;
    
    tt_orig.clear();
    searcher_orig.nodes_searched = 0;
    
    std::cout << "Depth 2:" << std::endl;
    best_d2 = searcher_orig.find_best_move(board, 2, -1.0, -1, 0, -1, {}, 1, &tt_orig, &stop, 1, nullptr);
    std::cout << "  nodes=" << searcher_orig.nodes_searched << " best=" << best_d2.to_uci() << std::endl;
    
    return 0;
}
