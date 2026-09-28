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
    
    std::string fen = "rnbqkbnr/ppppp1p1/8/Q4p1p/8/2P5/PP1PPPPP/RNB1KBNR b KQkq - 1 3";
    Board board = board_from_fen(fen);
    
    std::cout << "Position: " << fen << std::endl;
    board.print_board();
    
    std::cout << "\nBlack king: " << square_name(board.king_square(BLACK)) << std::endl;
    
    // h5 is at index 39
    std::cout << "Piece at h5 (39): " << piece_symbol(board.get_piece(39)) << std::endl;
    
    MoveList old_moves, new_moves;
    generate_legal_moves_into(board, old_moves);
    generate_legal_moves_into_new(board, new_moves);
    
    std::cout << "\nOLD: " << old_moves.size() << " moves" << std::endl;
    std::cout << "NEW: " << new_moves.size() << " moves" << std::endl;
    
    std::cout << "\nPawn on h5 (39) in OLD:" << std::endl;
    for (int i = 0; i < old_moves.size(); i++) {
        if (old_moves[i].from_sq == 39) {
            std::cout << "  " << old_moves[i].to_uci() << std::endl;
        }
    }
    
    std::cout << "\nPawn on h5 (39) in NEW:" << std::endl;
    for (int i = 0; i < new_moves.size(); i++) {
        if (new_moves[i].from_sq == 39) {
            std::cout << "  " << new_moves[i].to_uci() << std::endl;
        }
    }
    
    // Test h5h4
    Move h5h4(39, 31);
    std::cout << "\nTesting h5h4 (" << h5h4.to_uci() << "):" << std::endl;
    
    Board test = board;
    UndoInfo undo = make_move(test, h5h4);
    
    std::cout << "After move:" << std::endl;
    test.print_board();
    
    std::cout << "\nIs black in check? " << (king_in_check(test, BLACK) ? "YES" : "NO") << std::endl;
    
    unmake_move(test, h5h4, undo);
    
    return 0;
}
