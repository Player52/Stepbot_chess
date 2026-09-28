#include "board.h"
#include "movegen.h"
#include "zobrist.h"
#include <algorithm>
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
            board.squares[sq(file++, rank)] = fen_piece(c);
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

static bool compare_move_lists_ordered(MoveList& old_moves, MoveList& new_moves) {
    if (old_moves.size() != new_moves.size()) {
        std::cout << "  Different sizes: " << old_moves.size() << " vs " << new_moves.size() << std::endl;
        return false;
    }
    
    for (int i = 0; i < old_moves.size(); i++) {
        if (!(old_moves[i] == new_moves[i])) {
            std::cout << "  First difference at " << i << ": " 
                      << old_moves[i].to_uci() << " vs " << new_moves[i].to_uci() << std::endl;
            return false;
        }
    }
    return true;
}

static void print_move_list(const std::string& label, MoveList& moves) {
    std::cout << label << " (" << moves.size() << " moves): ";
    for (int i = 0; i < moves.size(); i++) {
        std::cout << moves[i].to_uci();
        if (i < moves.size() - 1) std::cout << " ";
    }
    std::cout << std::endl;
}

int main() {
    init_zobrist();
    
    // Test startpos
    Board startpos = board_from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    
    // Make e2e4 move
    Board after_e2e4 = startpos;
    Move e2e4_move = Move(12, 28); // e2 to e4
    make_move(after_e2e4, e2e4_move);
    
    std::cout << "Testing position after e2e4 from startpos:" << std::endl;
    after_e2e4.print_board();
    
    // Generate moves with both implementations
    MoveList old_moves, new_moves;
    generate_legal_moves_into(after_e2e4, old_moves);
    generate_legal_moves_into_new(after_e2e4, new_moves);
    
    bool same = compare_move_lists_ordered(old_moves, new_moves);
    std::cout << "  Same: " << (same ? "YES" : "NO") << std::endl;
    
    if (!same) {
        print_move_list("  Old", old_moves);
        print_move_list("  New", new_moves);
    }
    
    std::cout << std::endl;
    
    // Test a few more moves from startpos
    Board startpos2 = board_from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    
    // Make d2d4 move
    Board after_d2d4 = startpos2;
    Move d2d4_move = Move(11, 27); // d2 to d4
    make_move(after_d2d4, d2d4_move);
    
    std::cout << "Testing position after d2d4 from startpos:" << std::endl;
    
    old_moves.clear();
    new_moves.clear();
    generate_legal_moves_into(after_d2d4, old_moves);
    generate_legal_moves_into_new(after_d2d4, new_moves);
    
    same = compare_move_lists_ordered(old_moves, new_moves);
    std::cout << "  Same: " << (same ? "YES" : "NO") << std::endl;
    
    if (!same) {
        print_move_list("  Old", old_moves);
        print_move_list("  New", new_moves);
    }
    
    return 0;
}