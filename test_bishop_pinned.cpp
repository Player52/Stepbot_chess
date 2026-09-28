#include "board.h"
#include "movegen.h"
#include "zobrist.h"
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
    
    std::string fen = "rn1qkbnr/ppp1pppp/4b3/3p4/P7/4R3/1PPPPPPP/1NBQKBNR b Kkq - 3 3";
    Board board = board_from_fen(fen);
    
    std::cout << "Position: " << fen << std::endl;
    board.print_board();
    
    std::cout << "\nBlack king at: " << square_name(board.king_square(BLACK)) << std::endl;
    std::cout << "White rook at e3 (index 28): " << piece_symbol(board.get_piece(28)) << std::endl;
    std::cout << "Black bishop at e6 (index 52): " << piece_symbol(board.get_piece(52)) << std::endl;
    
    // Test e6d7: e6 (52) -> d7 (51)
    Move e6d7(52, 51);
    std::cout << "\nTesting e6d7 (" << e6d7.to_uci() << "):" << std::endl;
    
    Board test = board;
    UndoInfo undo = make_move(test, e6d7);
    
    std::cout << "After move:" << std::endl;
    test.print_board();
    
    std::cout << "\nIs black in check? " << (king_in_check(test, BLACK) ? "YES" : "NO") << std::endl;
    std::cout << "Is black king square attacked by white? " 
              << (square_attacked_by(test, test.king_square(BLACK), WHITE) ? "YES" : "NO") << std::endl;
    
    unmake_move(test, e6d7, undo);
    
    // Test e6c8: e6 (52) -> c8 (50)
    Move e6c8(52, 50);
    std::cout << "\nTesting e6c8 (" << e6c8.to_uci() << "):" << std::endl;
    
    test = board;
    undo = make_move(test, e6c8);
    
    std::cout << "After move:" << std::endl;
    test.print_board();
    
    std::cout << "\nIs black in check? " << (king_in_check(test, BLACK) ? "YES" : "NO") << std::endl;
    
    unmake_move(test, e6c8, undo);
    
    // Check if black is in check at the starting position
    std::cout << "\nAt starting position, is black in check? " << (king_in_check(board, BLACK) ? "YES" : "NO") << std::endl;
    
    MoveList old_moves, new_moves;
    generate_legal_moves_into(board, old_moves);
    generate_legal_moves_into_new(board, new_moves);
    
    std::cout << "\nOLD: " << old_moves.size() << " moves" << std::endl;
    std::cout << "NEW: " << new_moves.size() << " moves" << std::endl;
    
    std::cout << "\nBishop (52) moves in OLD:" << std::endl;
    for (int i = 0; i < old_moves.size(); i++) {
        if (old_moves[i].from_sq == 52) {
            std::cout << "  " << old_moves[i].to_uci() << std::endl;
        }
    }
    
    std::cout << "\nBishop (52) moves in NEW:" << std::endl;
    for (int i = 0; i < new_moves.size(); i++) {
        if (new_moves[i].from_sq == 52) {
            std::cout << "  " << new_moves[i].to_uci() << std::endl;
        }
    }
    
    return 0;
}
