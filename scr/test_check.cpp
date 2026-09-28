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
    
    std::string fen = "rnb1kbnr/pp1ppppp/8/q1p5/1P6/N7/P1PPPPPP/R1BQKBNR w KQkq - 1 3";
    Board board = board_from_fen(fen);
    
    std::cout << "Board:" << std::endl;
    board.print_board();
    
    std::cout << "\nWhite king at: " << board.king_square(WHITE) 
              << " (" << square_name(board.king_square(WHITE)) << ")" << std::endl;
    std::cout << "Black king at: " << board.king_square(BLACK) 
              << " (" << square_name(board.king_square(BLACK)) << ")" << std::endl;
    
    // Check if white is in check
    std::cout << "\nIs white in check? " << (king_in_check(board, WHITE) ? "YES" : "NO") << std::endl;
    std::cout << "Is black in check? " << (king_in_check(board, BLACK) ? "YES" : "NO") << std::endl;
    
    // Check if a5 attacks e1
    std::cout << "\nIs e1 attacked by black? " << (square_attacked_by(board, 60, BLACK) ? "YES" : "NO") << std::endl;
    std::cout << "Is a5 attacked by white? " << (square_attacked_by(board, 24, WHITE) ? "YES" : "NO") << std::endl;
    
    // Now make b4b5 and check
    Move b4b5 = Move(33, 25); // b4 to b5
    Board test = board;
    UndoInfo undo = make_move(test, b4b5);
    
    std::cout << "\nAfter b4b5:" << std::endl;
    test.print_board();
    
    std::cout << "\nIs white in check after b4b5? " << (king_in_check(test, WHITE) ? "YES" : "NO") << std::endl;
    std::cout << "Is e1 attacked by black? " << (square_attacked_by(test, 60, BLACK) ? "YES" : "NO") << std::endl;
    
    unmake_move(test, b4b5, undo);
    
    // Also check the piece at 24 and 26
    std::cout << "\nSquare 24 (a5): " << board.get_piece(24) << " (" << piece_symbol(board.get_piece(24)) << ")" << std::endl;
    std::cout << "Square 26 (c5): " << board.get_piece(26) << " (" << piece_symbol(board.get_piece(26)) << ")" << std::endl;
    std::cout << "Square 33 (b4): " << board.get_piece(33) << " (" << piece_symbol(board.get_piece(33)) << ")" << std::endl;
    std::cout << "Square 60 (e1): " << board.get_piece(60) << " (" << piece_symbol(board.get_piece(60)) << ")" << std::endl;
    
    return 0;
}
