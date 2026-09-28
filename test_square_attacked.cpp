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
    
    // Make b4b5 move
    Move b4b5(25, 17);
    Board test = board;
    UndoInfo undo = make_move(test, b4b5);
    
    std::cout << "After b4b5:" << std::endl;
    test.print_board();
    
    int king_sq = test.king_square(WHITE);
    std::cout << "\nWhite king at: " << square_name(king_sq) << " (index " << king_sq << ")" << std::endl;
    
    // Check diagonal attacks from queen at 32
    std::cout << "\nBlack queen at: " << square_name(32) << " (index 32)" << std::endl;
    std::cout << "Piece at 32: " << piece_symbol(test.get_piece(32)) << std::endl;
    
    // Manually check the diagonal from 32 to 4
    static const int diag[] = {9, 7, -7, -9};
    for (int direction : diag) {
        int curr = 32;
        std::cout << "\nChecking direction " << direction << " from " << square_name(32) << ":" << std::endl;
        while (true) {
            curr += direction;
            if (curr < 0 || curr >= 64) {
                std::cout << "  Out of bounds at " << curr << std::endl;
                break;
            }
            
            int prev_file = file_of(curr - direction);
            int new_file = file_of(curr);
            if (direction ==  9 && new_file <= prev_file) {
                std::cout << "  Wrapped (9): prev_file=" << prev_file << " new_file=" << new_file << std::endl;
                break;
            }
            if (direction == -7 && new_file <= prev_file) {
                std::cout << "  Wrapped (-7): prev_file=" << prev_file << " new_file=" << new_file << std::endl;
                break;
            }
            if (direction ==  7 && new_file >= prev_file) {
                std::cout << "  Wrapped (7): prev_file=" << prev_file << " new_file=" << new_file << std::endl;
                break;
            }
            if (direction == -9 && new_file >= prev_file) {
                std::cout << "  Wrapped (-9): prev_file=" << prev_file << " new_file=" << new_file << std::endl;
                break;
            }
            
            std::cout << "  Square " << square_name(curr) << " (" << curr << "): " << piece_symbol(test.get_piece(curr)) << std::endl;
            
            if (test.get_piece(curr) != EMPTY) {
                if (curr == king_sq) {
                    std::cout << "  Found king at " << square_name(curr) << std::endl;
                }
                break;
            }
        }
    }
    
    std::cout << "\n\nIs e1 attacked by black (using square_attacked_by)? " 
              << (square_attacked_by(test, king_sq, BLACK) ? "YES" : "NO") << std::endl;
    
    // Check each diagonal direction manually
    std::cout << "\nManual diagonal check from e1:" << std::endl;
    for (int direction : diag) {
        int curr = king_sq;
        std::cout << "  Direction " << direction << ": ";
        while (true) {
            int prev_file = file_of(curr);
            curr += direction;
            if (curr < 0 || curr >= 64) {
                std::cout << "(out of bounds)";
                break;
            }
            int new_file = file_of(curr);
            if (direction ==  9 && new_file <= prev_file) { std::cout << "(wrapped)"; break; }
            if (direction == -7 && new_file <= prev_file) { std::cout << "(wrapped)"; break; }
            if (direction ==  7 && new_file >= prev_file) { std::cout << "(wrapped)"; break; }
            if (direction == -9 && new_file >= prev_file) { std::cout << "(wrapped)"; break; }
            
            int piece = test.get_piece(curr);
            if (piece != EMPTY) {
                if (piece == BLACK * BISHOP || piece == BLACK * QUEEN) {
                    std::cout << "Attacked by " << piece_symbol(piece) << " at " << square_name(curr);
                } else {
                    std::cout << "Blocked by " << piece_symbol(piece) << " at " << square_name(curr);
                }
                break;
            }
        }
        std::cout << std::endl;
    }
    
    return 0;
}
