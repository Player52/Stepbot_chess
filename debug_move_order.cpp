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
        return false;
    }
    
    for (int i = 0; i < old_moves.size(); i++) {
        if (!(old_moves[i] == new_moves[i])) {
            return false;
        }
    }
    return true;
}

static bool compare_move_lists_unordered(MoveList& old_moves, MoveList& new_moves) {
    if (old_moves.size() != new_moves.size()) {
        return false;
    }
    
    for (int i = 0; i < old_moves.size(); i++) {
        bool found = false;
        for (int j = 0; j < new_moves.size(); j++) {
            if (old_moves[i] == new_moves[j]) {
                found = true;
                break;
            }
        }
        if (!found) {
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

static void test_move_order() {
    const std::vector<std::string> test_fens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",  // startpos
    };
    
    for (const std::string& fen : test_fens) {
        Board board = board_from_fen(fen);
        
        MoveList old_moves, new_moves;
        generate_legal_moves_into(board, old_moves);
        generate_legal_moves_into_new(board, new_moves);
        
        bool same_ordered = compare_move_lists_ordered(old_moves, new_moves);
        bool same_unordered = compare_move_lists_unordered(old_moves, new_moves);
        
        std::cout << "Position: " << fen.substr(0, 40) << "..." << std::endl;
        std::cout << "  Same ordered: " << (same_ordered ? "YES" : "NO") << std::endl;
        std::cout << "  Same unordered: " << (same_unordered ? "YES" : "NO") << std::endl;
        
        if (!same_ordered) {
            std::cout << "  [ORDER DIFFERENCE DETECTED]" << std::endl;
            print_move_list("  Old", old_moves);
            print_move_list("  New", new_moves);
            
            // Find the first difference
            for (int i = 0; i < std::min(old_moves.size(), new_moves.size()); i++) {
                if (!(old_moves[i] == new_moves[i])) {
                    std::cout << "  First difference at index " << i << ": " 
                              << old_moves[i].to_uci() << " vs " << new_moves[i].to_uci() << std::endl;
                    break;
                }
            }
        }
        std::cout << std::endl;
    }
}

int main() {
    init_zobrist();
    test_move_order();
    return 0;
}