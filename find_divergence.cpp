#include "board.h"
#include "movegen.h"
#include "zobrist.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>

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

static std::string board_to_fen(const Board& board) {
    std::ostringstream fen;
    
    // Piece placement
    for (int rank = 7; rank >= 0; rank--) {
        int empty_count = 0;
        for (int file = 0; file < 8; file++) {
            int sq_idx = ::sq(file, rank);
            int piece = board.get_piece(sq_idx);
            if (piece == EMPTY) {
                empty_count++;
            } else {
                if (empty_count > 0) {
                    fen << empty_count;
                    empty_count = 0;
                }
                char piece_char;
                switch (std::abs(piece)) {
                    case PAWN:   piece_char = 'P'; break;
                    case KNIGHT: piece_char = 'N'; break;
                    case BISHOP: piece_char = 'B'; break;
                    case ROOK:   piece_char = 'R'; break;
                    case QUEEN:  piece_char = 'Q'; break;
                    case KING:   piece_char = 'K'; break;
                    default:      piece_char = '.'; break;
                }
                if (piece < 0) piece_char = tolower(piece_char);
                fen << piece_char;
            }
        }
        if (empty_count > 0) fen << empty_count;
        if (rank > 0) fen << '/';
    }
    
    // Side to move
    fen << ' ' << (board.turn == WHITE ? "w" : "b") << ' ';
    
    // Castling
    bool has_castling = false;
    if (board.castling_rights.K) { fen << 'K'; has_castling = true; }
    if (board.castling_rights.Q) { fen << 'Q'; has_castling = true; }
    if (board.castling_rights.k) { fen << 'k'; has_castling = true; }
    if (board.castling_rights.q) { fen << 'q'; has_castling = true; }
    if (!has_castling) fen << '-';
    fen << ' ';
    
    // En passant
    if (board.en_passant_sq == -1)
        fen << "-";
    else
        fen << square_name(board.en_passant_sq);
    fen << ' ';
    
    // Halfmove clock and fullmove
    fen << board.halfmove_clock << ' ' << board.fullmove_number;
    
    return fen.str();
}

static void sort_move_list(MoveList& moves) {
    std::sort(moves.moves.begin(), moves.moves.begin() + moves.size(), 
        [](const Move& a, const Move& b) {
            if (a.from_sq != b.from_sq) return a.from_sq < b.from_sq;
            if (a.to_sq != b.to_sq) return a.to_sq < b.to_sq;
            return a.promotion < b.promotion;
        });
}

static bool move_lists_equal(MoveList& a, MoveList& b) {
    if (a.size() != b.size()) return false;
    sort_move_list(a);
    sort_move_list(b);
    for (int i = 0; i < a.size(); i++) {
        if (!(a[i] == b[i])) return false;
    }
    return true;
}

static void print_move_list(const std::string& label, MoveList& moves) {
    std::cout << label << " (" << moves.size() << "):";
    for (int i = 0; i < moves.size(); i++) {
        std::cout << " " << moves[i].to_uci();
    }
    std::cout << std::endl;
}

// Recursively explore all positions up to a certain depth
bool explore_position(const Board& parent_board, const Move& parent_move, int depth_remaining, int max_depth, std::vector<std::string>& path) {
    Board board = parent_board;
    if (parent_move.from_sq != 0 || parent_move.to_sq != 0) {
        // Make the move on a copy
        Board temp = parent_board;
        UndoInfo undo = make_move(temp, parent_move);
        board = temp;
    }
    
    // Generate moves with both implementations
    MoveList old_moves, new_moves;
    generate_legal_moves_into(board, old_moves);
    generate_legal_moves_into_new(board, new_moves);
    
    bool equal = move_lists_equal(old_moves, new_moves);
    
    if (!equal) {
        std::string fen = board_to_fen(board);
        std::cout << "\n" << std::string(80, '=') << std::endl;
        std::cout << "DIVERGENCE FOUND!" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << "Path to this position:" << std::endl;
        for (const auto& p : path) {
            std::cout << "  " << p << std::endl;
        }
        std::cout << "\nFinal move leading here: " << parent_move.to_uci() << std::endl;
        std::cout << "\nBoard state:" << std::endl;
        board.print_board();
        std::cout << "\nFEN: " << fen << std::endl;
        std::cout << "Turn: " << (board.turn == WHITE ? "WHITE" : "BLACK") << std::endl;
        std::cout << "Castling: K=" << board.castling_rights.K 
                  << " Q=" << board.castling_rights.Q
                  << " k=" << board.castling_rights.k
                  << " q=" << board.castling_rights.q << std::endl;
        std::cout << "En passant: " << board.en_passant_sq << " (" 
                  << (board.en_passant_sq == -1 ? "-" : square_name(board.en_passant_sq)) << ")" << std::endl;
        std::cout << "\nMove lists:" << std::endl;
        print_move_list("  OLD", old_moves);
        print_move_list("  NEW", new_moves);
        
        // Find differences
        std::cout << "\n  Moves only in OLD:";
        for (int i = 0; i < old_moves.size(); i++) {
            bool found = false;
            for (int j = 0; j < new_moves.size(); j++) {
                if (old_moves[i] == new_moves[j]) { found = true; break; }
            }
            if (!found) std::cout << " " << old_moves[i].to_uci();
        }
        std::cout << std::endl;
        
        std::cout << "  Moves only in NEW:";
        for (int i = 0; i < new_moves.size(); i++) {
            bool found = false;
            for (int j = 0; j < old_moves.size(); j++) {
                if (new_moves[i] == old_moves[j]) { found = true; break; }
            }
            if (!found) std::cout << " " << new_moves[i].to_uci();
        }
        std::cout << std::endl;
        
        std::cout << std::string(80, '=') << std::endl;
        return true; // Found divergence
    }
    
    // If we've reached max depth without divergence, stop
    if (depth_remaining <= 0) {
        return false;
    }
    
    // Explore child positions
    for (const Move& move : old_moves) {
        path.push_back(move.to_uci());
        if (explore_position(board, move, depth_remaining - 1, max_depth, path)) {
            return true;
        }
        path.pop_back();
    }
    
    return false;
}

int main() {
    init_zobrist();
    
    std::string start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    Board root = board_from_fen(start_fen);
    
    std::cout << "Starting divergence search from: " << start_fen << std::endl;
    std::cout << "Exploring all positions up to depth 4 (to find where NEW produces 0 moves)..." << std::endl;
    
    std::vector<std::string> path;
    path.push_back("ROOT");
    
    // First check root
    MoveList old_root, new_root;
    generate_legal_moves_into(root, old_root);
    generate_legal_moves_into_new(root, new_root);
    
    std::cout << "Root: OLD=" << old_root.size() << " moves, NEW=" << new_root.size() << " moves - " 
              << (move_lists_equal(old_root, new_root) ? "MATCH" : "MISMATCH") << std::endl;
    
    // Search for divergence in child positions
    // Use a dummy move to start
    Move dummy;
    dummy.from_sq = 0;
    dummy.to_sq = 0;
    
    bool found = explore_position(root, dummy, 4, 4, path);
    
    if (!found) {
        std::cout << "\nNo divergence found within depth 4. Searching deeper..." << std::endl;
        // Try depth 5
        path.clear();
        path.push_back("ROOT");
        found = explore_position(root, dummy, 5, 5, path);
    }
    
    if (!found) {
        std::cout << "\nNo divergence found within depth 5. The bug may be in a specific move ordering or TT path." << std::endl;
        std::cout << "Need to instrument actual alpha-beta search." << std::endl;
    }
    
    return 0;
}
