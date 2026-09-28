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
    
    std::cout << "Testing bishop at e6 (44)" << std::endl;
    std::cout << "Board:" << std::endl;
    board.print_board();
    
    // Generate pseudo-legal moves for the bishop
    MoveList pseudo;
    generate_pseudo_legal_moves_into(board, pseudo);
    
    std::cout << "\nPseudo-legal moves for bishop at 44:" << std::endl;
    for (int i = 0; i < pseudo.size(); i++) {
        if (pseudo[i].from_sq == 44) {
            std::cout << "  " << pseudo[i].to_uci() << std::endl;
        }
    }
    
    // Generate legal moves
    MoveList old_legal, new_legal;
    generate_legal_moves_into(board, old_legal);
    generate_legal_moves_into_new(board, new_legal);
    
    std::cout << "\nOLD legal moves for bishop at 44:" << std::endl;
    for (int i = 0; i < old_legal.size(); i++) {
        if (old_legal[i].from_sq == 44) {
            std::cout << "  " << old_legal[i].to_uci() << std::endl;
        }
    }
    
    std::cout << "\nNEW legal moves for bishop at 44:" << std::endl;
    for (int i = 0; i < new_legal.size(); i++) {
        if (new_legal[i].from_sq == 44) {
            std::cout << "  " << new_legal[i].to_uci() << std::endl;
        }
    }
    
    // Check if each pseudo-legal move is legal according to NEW
    std::cout << "\nChecking each pseudo-legal bishop move with NEW:" << std::endl;
    for (int i = 0; i < pseudo.size(); i++) {
        if (pseudo[i].from_sq == 44) {
            Board test = board;
            UndoInfo undo = make_move(test, pseudo[i]);
            bool in_check = king_in_check(test, BLACK);
            unmake_move(test, pseudo[i], undo);
            std::cout << "  " << pseudo[i].to_uci() << ": " 
                      << (in_check ? "ILLEGAL (leaves king in check)" : "legal") << std::endl;
        }
    }
    
    return 0;
}
