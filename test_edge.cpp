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

int main() {
    init_zobrist();
    
    // Test edge case: piece on a1, looking in direction 7 (up-left)
    // a1 = 56 (file 0, rank 7)
    // direction 7: up-left (file-1, rank+1)
    // from a1, going in direction 7: 56 + 7 = 63 (h8)
    
    std::string fen = "k7/8/8/8/8/8/8/7K w - - 0 1";
    Board board = board_from_fen(fen);
    
    std::cout << "Board:" << std::endl;
    board.print_board();
    
    std::cout << "\nWhite king: " << board.king_square(WHITE) << std::endl;
    std::cout << "Black king: " << board.king_square(BLACK) << std::endl;
    
    MoveList old_moves, new_moves;
    generate_legal_moves_into(board, old_moves);
    generate_legal_moves_into_new(board, new_moves);
    
    std::cout << "Old: " << old_moves.size() << " moves" << std::endl;
    std::cout << "New: " << new_moves.size() << " moves" << std::endl;
    
    return 0;
}
