#include "board.h"
#include "movegen.h"
#include "zobrist.h"
#include <iostream>
#include <algorithm>
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

static void sort_move_list(MoveList& moves) {
    std::sort(moves.moves.begin(), moves.moves.begin() + moves.size(), 
        [](const Move& a, const Move& b) {
            if (a.from_sq != b.from_sq) return a.from_sq < b.from_sq;
            if (a.to_sq != b.to_sq) return a.to_sq < b.to_sq;
            return a.promotion < b.promotion;
        });
}

static bool compare_move_lists(MoveList& old_moves, MoveList& new_moves) {
    if (old_moves.size() != new_moves.size()) {
        return false;
    }
    
    sort_move_list(old_moves);
    sort_move_list(new_moves);
    
    for (int i = 0; i < old_moves.size(); i++) {
        if (!(old_moves[i] == new_moves[i])) {
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
    
    // Test pinned piece positions
    const std::vector<std::string> test_fens = {
        // White rook on a1, white king on b1, black rook on a8
        // Rook on a1 should be pinned (can't move vertically)
        "R1k5/8/8/8/8/8/8/r1K5 w - - 0 1",
        
        // White bishop on c1, white king on d1, black bishop on h6
        // Bishop on c1 should be pinned along diagonal
        "3b4/8/8/8/8/5b2/8/K1B5 w - - 0 1",
        
        // Edge case: piece on edge of board
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        
        // White pawn on a2, white king on b1, black rook on a1
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKB1R w KQk - 0 1",
    };
    
    for (const std::string& fen : test_fens) {
        Board board = board_from_fen(fen);
        
        std::cout << "\nPosition: " << fen << std::endl;
        board.print_board();
        
        MoveList old_moves, new_moves;
        generate_legal_moves_into(board, old_moves);
        generate_legal_moves_into_new(board, new_moves);
        
        bool same = compare_move_lists(old_moves, new_moves);
        
        std::cout << "  Same: " << (same ? "YES" : "NO") << std::endl;
        
        if (!same) {
            print_move_list("  Old", old_moves);
            print_move_list("  New", new_moves);
            
            // Find specific differences
            std::cout << "  Differences:" << std::endl;
            std::cout << "    Only in old: ";
            for (int i = 0; i < old_moves.size(); i++) {
                bool found = false;
                for (int j = 0; j < new_moves.size(); j++) {
                    if (old_moves[i] == new_moves[j]) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::cout << old_moves[i].to_uci() << " ";
                }
            }
            std::cout << std::endl;
            
            std::cout << "    Only in new: ";
            for (int i = 0; i < new_moves.size(); i++) {
                bool found = false;
                for (int j = 0; j < old_moves.size(); j++) {
                    if (new_moves[i] == old_moves[j]) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::cout << new_moves[i].to_uci() << " ";
                }
            }
            std::cout << std::endl;
        }
    }
    
    return 0;
}
