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
    
    board.print_board();
    
    std::cout << "\nFinding all black bishops:" << std::endl;
    for (int sq = 0; sq < 64; sq++) {
        if (board.get_piece(sq) == -BISHOP) {
            std::cout << "  Black bishop at " << square_name(sq) << " (index " << sq << ")" << std::endl;
        }
    }
    
    std::cout << "\nFinding all black pieces at e6:" << std::endl;
    for (int sq = 0; sq < 64; sq++) {
        if (square_name(sq) == "e6") {
            std::cout << "  Square " << square_name(sq) << " (index " << sq << "): " 
                      << piece_symbol(board.get_piece(sq)) << std::endl;
        }
    }
    
    std::cout << "\nSquare at index 52: " << square_name(52) << " piece: " 
              << piece_symbol(board.get_piece(52)) << std::endl;
    
    std::cout << "Square at index 44: " << square_name(44) << " piece: " 
              << piece_symbol(board.get_piece(44)) << std::endl;
    
    return 0;
}
