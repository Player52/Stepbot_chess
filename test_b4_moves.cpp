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
    
    std::cout << "Testing b4 moves in position: " << fen << std::endl;
    board.print_board();
    
    // White pawn on b4 is at index 25
    std::cout << "\nWhite pawn on b4 is at index " << 25 << " (" << square_name(25) << ")" << std::endl;
    std::cout << "Piece at 25: " << piece_symbol(board.get_piece(25)) << std::endl;
    
    // Try b4b5: 25 -> 17
    Move b4b5(25, 17);
    std::cout << "\nTesting move b4b5 (" << b4b5.to_uci() << "):" << std::endl;
    
    Board test = board;
    UndoInfo undo = make_move(test, b4b5);
    
    std::cout << "After move:" << std::endl;
    test.print_board();
    
    std::cout << "\nWhite king at: " << square_name(test.king_square(WHITE)) << std::endl;
    std::cout << "Is white in check? " << (king_in_check(test, WHITE) ? "YES" : "NO") << std::endl;
    std::cout << "Is square " << square_name(test.king_square(WHITE)) << " attacked by black? " 
              << (square_attacked_by(test, test.king_square(WHITE), BLACK) ? "YES" : "NO") << std::endl;
    
    unmake_move(test, b4b5, undo);
    
    // Try b4xc5: 25 -> 34 (c5 is at index 34)
    Move b4c5(25, 34);
    std::cout << "\n\nTesting move b4c5 (" << b4c5.to_uci() << "):" << std::endl;
    
    test = board;
    undo = make_move(test, b4c5);
    
    std::cout << "After move:" << std::endl;
    test.print_board();
    
    std::cout << "\nWhite king at: " << square_name(test.king_square(WHITE)) << std::endl;
    std::cout << "Is white in check? " << (king_in_check(test, WHITE) ? "YES" : "NO") << std::endl;
    std::cout << "Is square " << square_name(test.king_square(WHITE)) << " attacked by black? " 
              << (square_attacked_by(test, test.king_square(WHITE), BLACK) ? "YES" : "NO") << std::endl;
    
    unmake_move(test, b4c5, undo);
    
    // Try b4xa5: 25 -> 32
    Move b4a5(25, 32);
    std::cout << "\n\nTesting move b4a5 (" << b4a5.to_uci() << "):" << std::endl;
    
    test = board;
    undo = make_move(test, b4a5);
    
    std::cout << "After move:" << std::endl;
    test.print_board();
    
    std::cout << "\nWhite king at: " << square_name(test.king_square(WHITE)) << std::endl;
    std::cout << "Is white in check? " << (king_in_check(test, WHITE) ? "YES" : "NO") << std::endl;
    std::cout << "Is square " << square_name(test.king_square(WHITE)) << " attacked by black? " 
              << (square_attacked_by(test, test.king_square(WHITE), BLACK) ? "YES" : "NO") << std::endl;
    
    unmake_move(test, b4a5, undo);
    
    return 0;
}
