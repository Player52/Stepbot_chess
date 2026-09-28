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

static void find_differences() {
    // Test positions that showed different behavior in the benchmark
    const std::vector<std::string> test_fens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",  // startpos
        "r3k2r/p1ppqpb1/bn2pnp1/2PPN3/1p2P3/2N2Q1p/PP1PBPPP/R3K2R w KQkq - 0 1",  // kiwipete
        "r5k1/1b3rbp/1p4p1/n7/P4P2/2P1p1P1/1Q2R2P/R1Bq2K1 w - - 1 25"  // tactical
    };
    
    for (const std::string& fen : test_fens) {
        Board board = board_from_fen(fen);
        
        MoveList old_moves, new_moves;
        generate_legal_moves_into(board, old_moves);
        generate_legal_moves_into_new(board, new_moves);
        
        bool same = compare_move_lists(old_moves, new_moves);
        
        std::cout << "Position: " << fen.substr(0, 40) << "..." << std::endl;
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
        std::cout << std::endl;
    }
}

int main() {
    init_zobrist();
    find_differences();
    return 0;
}